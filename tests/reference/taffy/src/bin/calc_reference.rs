//! Pinned calc oracle. TaffyTree's calc callback always returns zero, so this
//! LayoutPartialTree owns expressions and resolves their handles in mixed trees.
use serde::Deserialize;
use serde_json::json;
use std::collections::HashSet;
use std::io::{self, Read};
use taffy::compute::{
    compute_block_layout, compute_flexbox_layout, compute_grid_layout, compute_hidden_layout,
    compute_leaf_layout, compute_root_layout, BlockContext,
};
use taffy::prelude::*;
use taffy::style::{
    Direction, GridTemplateComponent, GridTemplateRepetition, GridTemplateTracks, RepetitionCount,
};
use taffy::tree::{
    Layout, LayoutBlockContainer, LayoutFlexboxContainer, LayoutGridContainer, LayoutInput,
    LayoutOutput, LayoutPartialTree, NodeId, RunMode, TraversePartialTree,
};
use taffy::CacheTree;
use taffy::DetailedGridInfo;

#[derive(Deserialize)]
#[serde(tag = "op", rename_all = "snake_case", deny_unknown_fields)]
enum Expression {
    Length {
        value: f32,
    },
    Percent {
        value: f32,
    },
    Add {
        left: Box<Self>,
        right: Box<Self>,
    },
    Subtract {
        left: Box<Self>,
        right: Box<Self>,
    },
    Scale {
        value: Box<Self>,
        factor: f32,
    },
    Min {
        left: Box<Self>,
        right: Box<Self>,
    },
    Max {
        left: Box<Self>,
        right: Box<Self>,
    },
    Clamp {
        lower: Box<Self>,
        value: Box<Self>,
        upper: Box<Self>,
    },
}

impl Expression {
    fn validate(&self, depth: usize) -> Result<(), String> {
        if depth > 64 {
            return Err("Calc expression depth exceeds 64".into());
        }
        let number = |value: f32| {
            if value.is_finite() && value.abs() <= 1_000_000.0 {
                Ok(())
            } else {
                Err("Calc expression number outside reference limits".into())
            }
        };
        match self {
            Self::Length { value } | Self::Percent { value } => number(*value),
            Self::Add { left, right }
            | Self::Subtract { left, right }
            | Self::Min { left, right }
            | Self::Max { left, right } => {
                left.validate(depth + 1)?;
                right.validate(depth + 1)
            }
            Self::Scale { value, factor } => {
                number(*factor)?;
                value.validate(depth + 1)
            }
            Self::Clamp {
                lower,
                value,
                upper,
            } => {
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
            Self::Clamp {
                lower,
                value,
                upper,
            } => lower
                .resolve(basis)
                .max(value.resolve(basis).min(upper.resolve(basis))),
        }
    }
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct CalcTrackInput {
    minimum: Option<Expression>,
    minimum_length: Option<f32>,
    maximum: Expression,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct CalcTemplateInput {
    kind: String,
    count: Option<u16>,
    tracks: Vec<CalcTrackInput>,
}

#[derive(Default, Deserialize)]
#[serde(default, deny_unknown_fields)]
struct InputStyle {
    display: Option<Display>,
    direction: Option<Direction>,
    intrinsic_width: Option<String>,
    item_is_replaced: bool,
    aspect_ratio: Option<f32>,
    content_box: bool,
    flex_grow: Option<f32>,
    gap_width: Option<Expression>,
    gap_height: Option<Expression>,
    flex_basis: Option<Expression>,
    grid_template_columns: Option<String>,
    grid_template_rows: Option<String>,
    calc_columns: Option<Vec<CalcTemplateInput>>,
    calc_rows: Option<Vec<CalcTemplateInput>>,
    calc_auto_columns: Option<Vec<CalcTrackInput>>,
    calc_auto_rows: Option<Vec<CalcTrackInput>>,
    grid_column_start: Option<i16>,
    grid_row_start: Option<i16>,
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
    content: Option<[f32; 2]>,
    style: Option<InputStyle>,
    nodes: Option<Vec<NodeInput>>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct NodeInput {
    id: u64,
    parent: Option<usize>,
    content: Option<[f32; 2]>,
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
            Self::Definite(value) if value.is_finite() && (0.0..=1_000_000.0).contains(&value) => {
                Ok(AvailableSpace::Definite(value))
            }
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

struct CalcNode {
    id: u64,
    parent: Option<usize>,
    style: Style<String>,
    content: Size<f32>,
    children: Vec<NodeId>,
    layout: Layout,
    grid: Option<DetailedGridInfo<String>>,
}

struct CalcTree {
    nodes: Vec<CalcNode>,
    expressions: Vec<Box<Expression>>,
}

impl CalcTree {
    fn new() -> Self {
        Self {
            nodes: Vec::new(),
            expressions: Vec::new(),
        }
    }

    fn push_node(&mut self, id: u64, parent: Option<usize>, content: [f32; 2]) {
        let index = self.nodes.len();
        if let Some(parent) = parent {
            self.nodes[parent].children.push(NodeId::new(index as u64));
        }
        self.nodes.push(CalcNode {
            id,
            parent,
            style: Style::default(),
            content: Size {
                width: content[0],
                height: content[1],
            },
            children: Vec::new(),
            layout: Layout::default(),
            grid: None,
        });
    }

    fn handle(&mut self, expression: Expression) -> Result<*const (), String> {
        expression.validate(0)?;
        let boxed = Box::new(expression);
        let handle = (&*boxed as *const Expression).cast();
        self.expressions.push(boxed);
        Ok(handle)
    }

    fn calc_tracks(
        &mut self,
        inputs: Vec<CalcTrackInput>,
    ) -> Result<Vec<TrackSizingFunction>, String> {
        if inputs.is_empty() || inputs.len() > 32 {
            return Err("Calc track list outside reference limits".into());
        }
        inputs
            .into_iter()
            .map(|input| {
                let min = match (input.minimum, input.minimum_length) {
                    (Some(expression), None) => {
                        MinTrackSizingFunction::calc(self.handle(expression)?)
                    }
                    (None, Some(value))
                        if value.is_finite() && (0.0..=1_000_000.0).contains(&value) =>
                    {
                        MinTrackSizingFunction::length(value)
                    }
                    _ => return Err("Invalid calc track minimum".into()),
                };
                Ok(TrackSizingFunction {
                    min,
                    max: MaxTrackSizingFunction::calc(self.handle(input.maximum)?),
                })
            })
            .collect()
    }

    fn calc_template(
        &mut self,
        inputs: Vec<CalcTemplateInput>,
    ) -> Result<Vec<GridTemplateComponent<String>>, String> {
        if inputs.is_empty() || inputs.len() > 32 {
            return Err("Calc template outside reference limits".into());
        }
        let mut result = Vec::new();
        for input in inputs {
            let tracks = self.calc_tracks(input.tracks)?;
            let count = match (input.kind.as_str(), input.count) {
                ("single", None) => {
                    result.extend(tracks.into_iter().map(GridTemplateComponent::Single));
                    continue;
                }
                ("count", Some(count)) if (1..=32).contains(&count) => {
                    RepetitionCount::Count(count)
                }
                ("auto-fill", None) => RepetitionCount::AutoFill,
                ("auto-fit", None) => RepetitionCount::AutoFit,
                _ => return Err("Invalid calc repetition".into()),
            };
            result.push(GridTemplateComponent::Repeat(GridTemplateRepetition {
                count,
                tracks,
                line_names: Vec::new(),
            }));
        }
        Ok(result)
    }

    fn dispatch(
        &mut self,
        node_id: NodeId,
        inputs: LayoutInput,
        block_ctx: Option<&mut BlockContext<'_>>,
    ) -> LayoutOutput {
        if inputs.run_mode == RunMode::PerformHiddenLayout {
            return compute_hidden_layout(self, node_id);
        }
        let index: usize = node_id.into();
        let display = self.nodes[index].style.display;
        let has_children = !self.nodes[index].children.is_empty();
        match (display, has_children) {
            (Display::None, _) => compute_hidden_layout(self, node_id),
            (Display::Block, true) => compute_block_layout(self, node_id, inputs, block_ctx),
            (Display::FlowRoot, true) => compute_block_layout(self, node_id, inputs, None),
            (Display::Flex, true) => compute_flexbox_layout(self, node_id, inputs),
            (Display::Grid, true) => compute_grid_layout(self, node_id, inputs),
            (_, false) => {
                let node = &self.nodes[index];
                compute_leaf_layout(
                    inputs,
                    &node.style,
                    |value, basis| self.resolve_calc_value(value, basis),
                    |known, _available| Size {
                        width: known.width.unwrap_or(node.content.width),
                        height: known.height.unwrap_or(node.content.height),
                    },
                )
            }
        }
    }
}

impl TraversePartialTree for CalcTree {
    type ChildIter<'a> = std::iter::Copied<std::slice::Iter<'a, NodeId>>;

    fn child_ids(&self, node_id: NodeId) -> Self::ChildIter<'_> {
        self.nodes[usize::from(node_id)].children.iter().copied()
    }
    fn child_count(&self, node_id: NodeId) -> usize {
        self.nodes[usize::from(node_id)].children.len()
    }
    fn get_child_id(&self, node_id: NodeId, index: usize) -> NodeId {
        self.nodes[usize::from(node_id)].children[index]
    }
}

impl LayoutPartialTree for CalcTree {
    type CoreContainerStyle<'a> = &'a Style<String>;
    type CustomIdent = String;

    fn get_core_container_style(&self, node_id: NodeId) -> Self::CoreContainerStyle<'_> {
        &self.nodes[usize::from(node_id)].style
    }

    fn resolve_calc_value(&self, val: *const (), basis: f32) -> f32 {
        // Every handle is a live Box<Expression> owned by this tree until layout ends.
        unsafe { &*val.cast::<Expression>() }.resolve(basis)
    }

    fn set_unrounded_layout(&mut self, node_id: NodeId, layout: &Layout) {
        self.nodes[usize::from(node_id)].layout = *layout;
    }

    fn compute_child_layout(&mut self, node_id: NodeId, inputs: LayoutInput) -> LayoutOutput {
        self.dispatch(node_id, inputs, None)
    }
}

impl LayoutBlockContainer for CalcTree {
    type BlockContainerStyle<'a> = &'a Style<String>;
    type BlockItemStyle<'a> = &'a Style<String>;

    fn get_block_container_style(&self, node_id: NodeId) -> Self::BlockContainerStyle<'_> {
        &self.nodes[usize::from(node_id)].style
    }
    fn get_block_child_style(&self, node_id: NodeId) -> Self::BlockItemStyle<'_> {
        &self.nodes[usize::from(node_id)].style
    }
    fn compute_block_child_layout(
        &mut self,
        node_id: NodeId,
        inputs: LayoutInput,
        block_ctx: Option<&mut BlockContext<'_>>,
    ) -> LayoutOutput {
        self.dispatch(node_id, inputs, block_ctx)
    }
}

impl LayoutFlexboxContainer for CalcTree {
    type FlexboxContainerStyle<'a> = &'a Style<String>;
    type FlexboxItemStyle<'a> = &'a Style<String>;

    fn get_flexbox_container_style(&self, node_id: NodeId) -> Self::FlexboxContainerStyle<'_> {
        &self.nodes[usize::from(node_id)].style
    }
    fn get_flexbox_child_style(&self, node_id: NodeId) -> Self::FlexboxItemStyle<'_> {
        &self.nodes[usize::from(node_id)].style
    }
}

impl LayoutGridContainer for CalcTree {
    type GridContainerStyle<'a> = &'a Style<String>;
    type GridItemStyle<'a> = &'a Style<String>;

    fn get_grid_container_style(&self, node_id: NodeId) -> Self::GridContainerStyle<'_> {
        &self.nodes[usize::from(node_id)].style
    }
    fn get_grid_child_style(&self, node_id: NodeId) -> Self::GridItemStyle<'_> {
        &self.nodes[usize::from(node_id)].style
    }
    fn set_detailed_grid_info(&mut self, node_id: NodeId, info: DetailedGridInfo<String>) {
        self.nodes[usize::from(node_id)].grid = Some(info);
    }
}

impl CacheTree for CalcTree {
    // Reference cases are small and intentionally recomputed for every query.
    fn cache_get(&mut self, _: NodeId, _: &LayoutInput) -> Option<LayoutOutput> {
        None
    }
    fn cache_store(&mut self, _: NodeId, _: &LayoutInput, _: LayoutOutput) {}
    fn cache_clear(&mut self, _: NodeId) {}
}

fn apply_style(tree: &mut CalcTree, index: usize, style: InputStyle) -> Result<(), String> {
    if (style.grid_template_columns.is_some() && style.calc_columns.is_some())
        || (style.grid_template_rows.is_some() && style.calc_rows.is_some())
    {
        return Err("Conflicting calc grid templates".into());
    }
    if let Some(display) = style.display {
        tree.nodes[index].style.display = display;
    }
    if style.content_box {
        tree.nodes[index].style.box_sizing = BoxSizing::ContentBox;
    }
    if let Some(direction) = style.direction {
        tree.nodes[index].style.direction = direction;
    }
    tree.nodes[index].style.item_is_replaced = style.item_is_replaced;
    if let Some(ratio) = style.aspect_ratio {
        if !ratio.is_finite() || ratio <= 0.0 || ratio > 1_000_000.0 {
            return Err("Invalid calc aspect ratio".into());
        }
        tree.nodes[index].style.aspect_ratio = Some(ratio);
    }
    if let Some(width) = style.intrinsic_width {
        if style.width.is_some()
            || !matches!(
                width.as_str(),
                "min-content" | "max-content" | "fit-content"
            )
        {
            return Err("Invalid calc intrinsic width".into());
        }
        tree.nodes[index].style.size.width = width.parse().map_err(|error| format!("{error:?}"))?;
    }
    if let Some(grow) = style.flex_grow {
        if !grow.is_finite() || !(0.0..=1_000_000.0).contains(&grow) {
            return Err("Invalid calc flex grow factor".into());
        }
        tree.nodes[index].style.flex_grow = grow;
    }
    if let Some(columns) = style.grid_template_columns {
        let tracks: GridTemplateTracks<String, GridTemplateComponent<String>> =
            columns.parse().map_err(|error| format!("{error:?}"))?;
        tree.nodes[index].style.grid_template_columns = tracks.tracks;
        tree.nodes[index].style.grid_template_column_names = tracks.line_names;
    }
    if let Some(rows) = style.grid_template_rows {
        let tracks: GridTemplateTracks<String, GridTemplateComponent<String>> =
            rows.parse().map_err(|error| format!("{error:?}"))?;
        tree.nodes[index].style.grid_template_rows = tracks.tracks;
        tree.nodes[index].style.grid_template_row_names = tracks.line_names;
    }
    if let Some(columns) = style.calc_columns {
        tree.nodes[index].style.grid_template_columns = tree.calc_template(columns)?;
    }
    if let Some(rows) = style.calc_rows {
        tree.nodes[index].style.grid_template_rows = tree.calc_template(rows)?;
    }
    if let Some(columns) = style.calc_auto_columns {
        tree.nodes[index].style.grid_auto_columns = tree.calc_tracks(columns)?;
    }
    if let Some(rows) = style.calc_auto_rows {
        tree.nodes[index].style.grid_auto_rows = tree.calc_tracks(rows)?;
    }
    if let Some(value) = style.grid_column_start {
        if value == 0 || value.unsigned_abs() > 128 {
            return Err("Invalid calc grid line".into());
        }
        tree.nodes[index].style.grid_column.start = GridPlacement::Line(value.into());
    }
    if let Some(value) = style.grid_row_start {
        if value == 0 || value.unsigned_abs() > 128 {
            return Err("Invalid calc grid line".into());
        }
        tree.nodes[index].style.grid_row.start = GridPlacement::Line(value.into());
    }
    macro_rules! bind {
        ($field:expr, $input:expr, $kind:ident) => {
            if let Some(expression) = $input {
                let handle = tree.handle(expression)?;
                $field = $kind::calc(handle);
            }
        };
    }
    bind!(tree.nodes[index].style.size.width, style.width, Dimension);
    bind!(tree.nodes[index].style.size.height, style.height, Dimension);
    bind!(
        tree.nodes[index].style.min_size.width,
        style.min_width,
        LengthPercentageAuto
    );
    bind!(
        tree.nodes[index].style.min_size.height,
        style.min_height,
        LengthPercentageAuto
    );
    bind!(
        tree.nodes[index].style.max_size.width,
        style.max_width,
        LengthPercentageAuto
    );
    bind!(
        tree.nodes[index].style.max_size.height,
        style.max_height,
        LengthPercentageAuto
    );
    bind!(
        tree.nodes[index].style.margin.left,
        style.margin_left,
        LengthPercentageAuto
    );
    bind!(
        tree.nodes[index].style.margin.top,
        style.margin_top,
        LengthPercentageAuto
    );
    bind!(
        tree.nodes[index].style.margin.right,
        style.margin_right,
        LengthPercentageAuto
    );
    bind!(
        tree.nodes[index].style.margin.bottom,
        style.margin_bottom,
        LengthPercentageAuto
    );
    bind!(
        tree.nodes[index].style.padding.left,
        style.padding_left,
        LengthPercentage
    );
    bind!(
        tree.nodes[index].style.padding.top,
        style.padding_top,
        LengthPercentage
    );
    bind!(
        tree.nodes[index].style.padding.right,
        style.padding_right,
        LengthPercentage
    );
    bind!(
        tree.nodes[index].style.padding.bottom,
        style.padding_bottom,
        LengthPercentage
    );
    bind!(
        tree.nodes[index].style.border.left,
        style.border_left,
        LengthPercentage
    );
    bind!(
        tree.nodes[index].style.border.top,
        style.border_top,
        LengthPercentage
    );
    bind!(
        tree.nodes[index].style.border.right,
        style.border_right,
        LengthPercentage
    );
    bind!(
        tree.nodes[index].style.border.bottom,
        style.border_bottom,
        LengthPercentage
    );
    bind!(
        tree.nodes[index].style.gap.width,
        style.gap_width,
        LengthPercentage
    );
    bind!(
        tree.nodes[index].style.gap.height,
        style.gap_height,
        LengthPercentage
    );
    bind!(
        tree.nodes[index].style.flex_basis,
        style.flex_basis,
        Dimension
    );
    Ok(())
}

fn run_case(case: Case) -> Result<serde_json::Value, String> {
    if case.name.is_empty() || case.name.len() > 100 {
        return Err("Invalid calc case name".into());
    }
    let tree_case = case.nodes.is_some();
    let inputs = match (case.content, case.style, case.nodes) {
        (Some(content), Some(style), None) => vec![NodeInput {
            id: 0,
            parent: None,
            content: Some(content),
            style,
        }],
        (None, None, Some(nodes)) if !nodes.is_empty() && nodes.len() <= 128 => nodes,
        _ => return Err("Calc case requires one leaf or a nonempty node tree".into()),
    };
    let [available_width, available_height] = case.available;
    let available_width = available_width.into_taffy()?;
    let available_height = available_height.into_taffy()?;
    let mut tree = CalcTree::new();
    let mut ids = HashSet::new();
    for (index, input) in inputs.into_iter().enumerate() {
        if !ids.insert(input.id)
            || (index == 0 && input.parent.is_some())
            || (index > 0 && !matches!(input.parent, Some(parent) if parent < index))
        {
            return Err("Invalid calc tree identity or parent order".into());
        }
        let content = input.content.unwrap_or([0.0, 0.0]);
        if content
            .into_iter()
            .any(|value| !value.is_finite() || !(0.0..=1_000_000.0).contains(&value))
        {
            return Err("Calc case dimensions outside reference limits".into());
        }
        tree.push_node(input.id, input.parent, content);
        apply_style(&mut tree, index, input.style)?;
    }
    compute_root_layout(
        &mut tree,
        NodeId::new(0),
        Size {
            width: available_width,
            height: available_height,
        },
    );
    let rect =
        |edges: taffy::geometry::Rect<f32>| [edges.left, edges.top, edges.right, edges.bottom];
    let mut nodes = Vec::new();
    for node in tree.nodes {
        let layout = node.layout;
        let values = [
            layout.location.x,
            layout.location.y,
            layout.size.width,
            layout.size.height,
            layout.margin.left,
            layout.margin.top,
            layout.margin.right,
            layout.margin.bottom,
            layout.padding.left,
            layout.padding.top,
            layout.padding.right,
            layout.padding.bottom,
            layout.border.left,
            layout.border.top,
            layout.border.right,
            layout.border.bottom,
        ];
        if values.into_iter().any(|value| !value.is_finite()) {
            return Err("Non-finite calc result".into());
        }
        if node.grid.as_ref().is_some_and(|info| {
            info.rows
                .positions
                .iter()
                .chain(info.columns.positions.iter())
                .any(|track| !track.start.is_finite() || !track.end.is_finite())
        }) {
            return Err("Non-finite calc grid track".into());
        }
        let grid = node.grid.map(|info| {
            let axis = |tracks: &taffy::DetailedGridTracksInfo<String>| json!({
                "negative": tracks.negative_implicit_tracks, "explicit": tracks.explicit_tracks,
                "positive": tracks.positive_implicit_tracks,
                "positions": tracks.positions.iter().map(|track| [track.start, track.end]).collect::<Vec<_>>(),
                "line_names": tracks.iter_line_names().collect::<Vec<_>>(),
            });
            json!({"rows": axis(&info.rows), "columns": axis(&info.columns),
                "items": info.items.iter().map(|item| [item.row_start, item.row_end,
                    item.column_start, item.column_end]).collect::<Vec<_>>()})
        });
        nodes.push(json!({"id": node.id, "parent": node.parent,
            "rect": [layout.location.x, layout.location.y,
                layout.size.width, layout.size.height],
            "margin": rect(layout.margin), "padding": rect(layout.padding),
            "border": rect(layout.border),
            "scrollbar": [layout.scrollbar_size.width, layout.scrollbar_size.height],
            "overflow": rect(layout.scrollable_overflow_rect), "grid": grid}));
    }
    if tree_case {
        Ok(json!({"name": case.name, "nodes": nodes}))
    } else {
        let leaf = &nodes[0];
        Ok(json!({"name": case.name, "rect": leaf["rect"],
            "margin": leaf["margin"], "padding": leaf["padding"],
            "border": leaf["border"]}))
    }
}

fn run() -> Result<(), String> {
    let mut input = String::new();
    io::stdin()
        .read_to_string(&mut input)
        .map_err(|error| error.to_string())?;
    let document: Document = serde_json::from_str(&input).map_err(|error| error.to_string())?;
    if document.protocol != "AXISLY_CALC_V1"
        || document.cases.is_empty()
        || document.cases.len() > 128
    {
        return Err("Invalid calc protocol or case count".into());
    }
    let mut names = HashSet::new();
    let mut cases = Vec::new();
    for case in document.cases {
        if !names.insert(case.name.clone()) {
            return Err("Duplicate calc case name".into());
        }
        cases.push(run_case(case)?);
    }
    println!(
        "{}",
        json!({"protocol": "AXISLY_CALC_V1", "taffy": "0.14.0", "cases": cases})
    );
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}
