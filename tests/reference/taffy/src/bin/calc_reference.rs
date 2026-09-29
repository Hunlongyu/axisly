//! Pinned single-node calc oracle. TaffyTree's calc callback always returns zero,
//! so this small LayoutPartialTree owns the expressions and resolves their handles.
use serde::Deserialize;
use serde_json::json;
use std::collections::HashSet;
use std::io::{self, Read};
use taffy::compute::{compute_leaf_layout, compute_root_layout};
use taffy::prelude::*;
use taffy::tree::{Layout, LayoutInput, LayoutOutput, LayoutPartialTree, NodeId, TraversePartialTree};

#[derive(Deserialize)]
#[serde(tag = "op", rename_all = "snake_case", deny_unknown_fields)]
enum Expression {
    Length { value: f32 },
    Percent { value: f32 },
    Add { left: Box<Self>, right: Box<Self> },
    Subtract { left: Box<Self>, right: Box<Self> },
    Scale { value: Box<Self>, factor: f32 },
    Min { left: Box<Self>, right: Box<Self> },
    Max { left: Box<Self>, right: Box<Self> },
    Clamp { lower: Box<Self>, value: Box<Self>, upper: Box<Self> },
}

impl Expression {
    fn validate(&self, depth: usize) -> Result<(), String> {
        if depth > 64 {
            return Err("Calc expression depth exceeds 64".into());
        }
        let number = |value: f32| {
            if value.is_finite() && value.abs() <= 1_000_000.0 { Ok(()) }
            else { Err("Calc expression number outside reference limits".into()) }
        };
        match self {
            Self::Length { value } | Self::Percent { value } => number(*value),
            Self::Add { left, right } | Self::Subtract { left, right }
            | Self::Min { left, right } | Self::Max { left, right } => {
                left.validate(depth + 1)?;
                right.validate(depth + 1)
            }
            Self::Scale { value, factor } => {
                number(*factor)?;
                value.validate(depth + 1)
            }
            Self::Clamp { lower, value, upper } => {
                lower.validate(depth + 1)?;
                value.validate(depth + 1)?;
                upper.validate(depth + 1)
            }
        }
    }

    fn resolve(&self, basis: f32) -> f32 {
        match self {
            Self::Length { value } => *value,
            Self::Percent { value } => value * basis,
            Self::Add { left, right } => left.resolve(basis) + right.resolve(basis),
            Self::Subtract { left, right } => left.resolve(basis) - right.resolve(basis),
            Self::Scale { value, factor } => value.resolve(basis) * factor,
            Self::Min { left, right } => left.resolve(basis).min(right.resolve(basis)),
            Self::Max { left, right } => left.resolve(basis).max(right.resolve(basis)),
            Self::Clamp { lower, value, upper } => lower.resolve(basis)
                .max(value.resolve(basis).min(upper.resolve(basis))),
        }
    }
}

#[derive(Default, Deserialize)]
#[serde(default, deny_unknown_fields)]
struct InputStyle {
    content_box: bool,
    width: Option<Expression>,
    height: Option<Expression>,
    min_width: Option<Expression>,
    min_height: Option<Expression>,
    max_width: Option<Expression>,
    max_height: Option<Expression>,
    margin_left: Option<Expression>,
    margin_top: Option<Expression>,
    margin_right: Option<Expression>,
    margin_bottom: Option<Expression>,
    padding_left: Option<Expression>,
    padding_top: Option<Expression>,
    padding_right: Option<Expression>,
    padding_bottom: Option<Expression>,
    border_left: Option<Expression>,
    border_top: Option<Expression>,
    border_right: Option<Expression>,
    border_bottom: Option<Expression>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Case {
    name: String,
    available: [SpaceInput; 2],
    content: [f32; 2],
    style: InputStyle,
}

#[derive(Deserialize)]
#[serde(untagged)]
enum SpaceInput {
    Definite(f32),
    Keyword(String),
}

impl SpaceInput {
    fn into_taffy(self) -> Result<AvailableSpace, String> {
        match self {
            Self::Definite(value) if value.is_finite()
                && (0.0..=1_000_000.0).contains(&value) => Ok(AvailableSpace::Definite(value)),
            Self::Keyword(value) if value == "min-content" => Ok(AvailableSpace::MinContent),
            Self::Keyword(value) if value == "max-content" => Ok(AvailableSpace::MaxContent),
            _ => Err("Invalid calc available space".into()),
        }
    }
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Document {
    protocol: String,
    cases: Vec<Case>,
}

struct CalcLeaf {
    style: Style<String>,
    content: Size<f32>,
    expressions: Vec<Box<Expression>>,
    layout: Layout,
}

impl CalcLeaf {
    fn new(content: [f32; 2]) -> Self {
        Self {
            style: Style::default(),
            content: Size { width: content[0], height: content[1] },
            expressions: Vec::new(),
            layout: Layout::default(),
        }
    }

    fn handle(&mut self, expression: Expression) -> Result<*const (), String> {
        expression.validate(0)?;
        let boxed = Box::new(expression);
        let handle = (&*boxed as *const Expression).cast();
        self.expressions.push(boxed);
        Ok(handle)
    }
}

impl TraversePartialTree for CalcLeaf {
    type ChildIter<'a> = std::iter::Empty<NodeId>;

    fn child_ids(&self, _: NodeId) -> Self::ChildIter<'_> { std::iter::empty() }
    fn child_count(&self, _: NodeId) -> usize { 0 }
    fn get_child_id(&self, _: NodeId, _: usize) -> NodeId {
        unreachable!("A calc reference leaf has no children")
    }
}

impl LayoutPartialTree for CalcLeaf {
    type CoreContainerStyle<'a> = &'a Style<String>;
    type CustomIdent = String;

    fn get_core_container_style(&self, _: NodeId) -> Self::CoreContainerStyle<'_> {
        &self.style
    }

    fn resolve_calc_value(&self, val: *const (), basis: f32) -> f32 {
        // Every handle is a live Box<Expression> owned by this tree until layout ends.
        unsafe { &*val.cast::<Expression>() }.resolve(basis)
    }

    fn set_unrounded_layout(&mut self, _: NodeId, layout: &Layout) {
        self.layout = *layout;
    }

    fn compute_child_layout(&mut self, _: NodeId, inputs: LayoutInput) -> LayoutOutput {
        compute_leaf_layout(inputs, &self.style,
            |value, basis| self.resolve_calc_value(value, basis),
            |known, _available| Size {
                width: known.width.unwrap_or(self.content.width),
                height: known.height.unwrap_or(self.content.height),
            })
    }
}

fn run_case(case: Case) -> Result<serde_json::Value, String> {
    if case.name.is_empty() || case.name.len() > 100 {
        return Err("Invalid calc case name".into());
    }
    if case.content.into_iter().any(|value|
        !value.is_finite() || !(0.0..=1_000_000.0).contains(&value)) {
        return Err("Calc case dimensions outside reference limits".into());
    }
    let [available_width, available_height] = case.available;
    let available_width = available_width.into_taffy()?;
    let available_height = available_height.into_taffy()?;
    let mut tree = CalcLeaf::new(case.content);
    let style = case.style;
    if style.content_box { tree.style.box_sizing = BoxSizing::ContentBox; }
    macro_rules! bind {
        ($field:expr, $input:expr, $kind:ident) => {
            if let Some(expression) = $input {
                $field = $kind::calc(tree.handle(expression)?);
            }
        };
    }
    bind!(tree.style.size.width, style.width, Dimension);
    bind!(tree.style.size.height, style.height, Dimension);
    bind!(tree.style.min_size.width, style.min_width, LengthPercentageAuto);
    bind!(tree.style.min_size.height, style.min_height, LengthPercentageAuto);
    bind!(tree.style.max_size.width, style.max_width, LengthPercentageAuto);
    bind!(tree.style.max_size.height, style.max_height, LengthPercentageAuto);
    bind!(tree.style.margin.left, style.margin_left, LengthPercentageAuto);
    bind!(tree.style.margin.top, style.margin_top, LengthPercentageAuto);
    bind!(tree.style.margin.right, style.margin_right, LengthPercentageAuto);
    bind!(tree.style.margin.bottom, style.margin_bottom, LengthPercentageAuto);
    bind!(tree.style.padding.left, style.padding_left, LengthPercentage);
    bind!(tree.style.padding.top, style.padding_top, LengthPercentage);
    bind!(tree.style.padding.right, style.padding_right, LengthPercentage);
    bind!(tree.style.padding.bottom, style.padding_bottom, LengthPercentage);
    bind!(tree.style.border.left, style.border_left, LengthPercentage);
    bind!(tree.style.border.top, style.border_top, LengthPercentage);
    bind!(tree.style.border.right, style.border_right, LengthPercentage);
    bind!(tree.style.border.bottom, style.border_bottom, LengthPercentage);
    compute_root_layout(&mut tree, NodeId::new(0), Size {
        width: available_width,
        height: available_height,
    });
    let layout = tree.layout;
    let rect = |edges: taffy::geometry::Rect<f32>| {
        [edges.left, edges.top, edges.right, edges.bottom]
    };
    let values = [layout.size.width, layout.size.height,
        layout.margin.left, layout.margin.top, layout.margin.right, layout.margin.bottom,
        layout.padding.left, layout.padding.top, layout.padding.right, layout.padding.bottom,
        layout.border.left, layout.border.top, layout.border.right, layout.border.bottom];
    if values.into_iter().any(|value| !value.is_finite()) {
        return Err("Non-finite calc result".into());
    }
    Ok(json!({"name": case.name,
        "rect": [layout.location.x, layout.location.y, layout.size.width, layout.size.height],
        "margin": rect(layout.margin), "padding": rect(layout.padding),
        "border": rect(layout.border)}))
}

fn run() -> Result<(), String> {
    let mut input = String::new();
    io::stdin().read_to_string(&mut input).map_err(|error| error.to_string())?;
    let document: Document = serde_json::from_str(&input).map_err(|error| error.to_string())?;
    if document.protocol != "AXISLY_CALC_V1" || document.cases.is_empty()
        || document.cases.len() > 128 {
        return Err("Invalid calc protocol or case count".into());
    }
    let mut names = HashSet::new();
    let mut cases = Vec::new();
    for case in document.cases {
        if !names.insert(case.name.clone()) { return Err("Duplicate calc case name".into()); }
        cases.push(run_case(case)?);
    }
    println!("{}", json!({"protocol": "AXISLY_CALC_V1", "taffy": "0.14.0", "cases": cases}));
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}
