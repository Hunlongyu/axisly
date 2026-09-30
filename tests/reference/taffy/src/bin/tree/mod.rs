//! Development-only tree oracle with its own protocol and executable.
use serde::de::{Error as DeError, MapAccess, SeqAccess, Visitor};
use serde::{Deserialize, Deserializer};
use serde_json::{json, Value};
use std::collections::HashSet;
use std::fmt;
use taffy::prelude::*;

mod style;
use style::{decode_style, style_fields};

type Result<T> = std::result::Result<T, String>;

#[derive(Clone, Copy, Default)]
struct Metrics {
    minimum: Size<f32>,
    preferred: Size<f32>,
    baseline: Option<f32>,
}

fn measure(known: Option<f32>, available: AvailableSpace, minimum: f32, preferred: f32) -> f32 {
    known.unwrap_or(match available {
        AvailableSpace::MinContent => minimum,
        AvailableSpace::MaxContent | AvailableSpace::Definite(_) => preferred,
    })
}

const TREE_PROTOCOL: &str = "AXISLY_TREE_V1";
const GRID_DETAILS_PROTOCOL: &str = "AXISLY_TREE_GRID_DETAILS_V1";
const FIELDS_PROTOCOL: &str = "AXISLY_TREE_STYLE_FIELDS_V1";

struct StrictValue(Value);

impl<'de> Deserialize<'de> for StrictValue {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> std::result::Result<Self, D::Error> {
        struct StrictVisitor;
        impl<'de> Visitor<'de> for StrictVisitor {
            type Value = StrictValue;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                formatter.write_str("JSON with unique object keys")
            }
            fn visit_bool<E: DeError>(self, value: bool) -> std::result::Result<Self::Value, E> {
                Ok(StrictValue(Value::Bool(value)))
            }
            fn visit_i64<E: DeError>(self, value: i64) -> std::result::Result<Self::Value, E> {
                Ok(StrictValue(Value::Number(value.into())))
            }
            fn visit_u64<E: DeError>(self, value: u64) -> std::result::Result<Self::Value, E> {
                Ok(StrictValue(Value::Number(value.into())))
            }
            fn visit_f64<E: DeError>(self, value: f64) -> std::result::Result<Self::Value, E> {
                serde_json::Number::from_f64(value)
                    .map(Value::Number)
                    .map(StrictValue)
                    .ok_or_else(|| E::custom("Non-finite JSON number"))
            }
            fn visit_str<E: DeError>(self, value: &str) -> std::result::Result<Self::Value, E> {
                Ok(StrictValue(Value::String(value.into())))
            }
            fn visit_string<E: DeError>(
                self,
                value: String,
            ) -> std::result::Result<Self::Value, E> {
                Ok(StrictValue(Value::String(value)))
            }
            fn visit_unit<E: DeError>(self) -> std::result::Result<Self::Value, E> {
                Ok(StrictValue(Value::Null))
            }
            fn visit_none<E: DeError>(self) -> std::result::Result<Self::Value, E> {
                Ok(StrictValue(Value::Null))
            }
            fn visit_seq<A: SeqAccess<'de>>(
                self,
                mut sequence: A,
            ) -> std::result::Result<Self::Value, A::Error> {
                let mut values = Vec::new();
                while let Some(StrictValue(value)) = sequence.next_element()? {
                    values.push(value);
                }
                Ok(StrictValue(Value::Array(values)))
            }
            fn visit_map<A: MapAccess<'de>>(
                self,
                mut map: A,
            ) -> std::result::Result<Self::Value, A::Error> {
                let mut values = serde_json::Map::new();
                while let Some((key, StrictValue(value))) =
                    map.next_entry::<String, StrictValue>()?
                {
                    if values.insert(key.clone(), value).is_some() {
                        return Err(A::Error::custom(format!("Duplicate JSON key: {key}")));
                    }
                }
                Ok(StrictValue(Value::Object(values)))
            }
        }
        deserializer.deserialize_any(StrictVisitor)
    }
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Document {
    protocol: String,
    cases: Vec<Case>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Case {
    name: String,
    available: [SpaceInput; 2],
    nodes: Vec<NodeInput>,
}

#[derive(Deserialize)]
#[serde(untagged)]
enum SpaceInput {
    Definite(f32),
    Keyword(String),
}

impl SpaceInput {
    fn into_taffy(self) -> Result<AvailableSpace> {
        match self {
            Self::Definite(value) if value.is_finite() && (0.0..=1_000_000.0).contains(&value) => {
                Ok(AvailableSpace::Definite(value))
            }
            Self::Keyword(value) if value == "min-content" => Ok(AvailableSpace::MinContent),
            Self::Keyword(value) if value == "max-content" => Ok(AvailableSpace::MaxContent),
            _ => Err("Invalid available space".into()),
        }
    }
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct NodeInput {
    id: u64,
    parent: Option<usize>,
    style: Value,
    measure: Option<MeasureInput>,
}

#[derive(Deserialize)]
#[serde(untagged)]
enum MeasureInput {
    Metrics(MetricsInput),
    Ahem(AhemInput),
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct MetricsInput {
    minimum: [f32; 2],
    preferred: [f32; 2],
    baseline: Option<f32>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct AhemInput {
    ahem_text: String,
    #[serde(default)]
    vertical: bool,
}

enum LeafContent {
    Metrics(Metrics),
    Ahem {
        segments: Vec<usize>,
        vertical: bool,
    },
}

impl LeafContent {
    fn from_input(input: &MeasureInput) -> Result<Self> {
        match input {
            MeasureInput::Metrics(input) => metrics(input).map(Self::Metrics),
            MeasureInput::Ahem(input) => {
                if input.ahem_text.len() > 4096 {
                    return Err("Ahem text outside reference limits".into());
                }
                Ok(Self::Ahem {
                    segments: input.ahem_text.split('\u{200b}').map(str::len).collect(),
                    vertical: input.vertical,
                })
            }
        }
    }

    fn measure(&self, known: Size<Option<f32>>, space: Size<AvailableSpace>) -> Size<f32> {
        match self {
            Self::Metrics(metrics) => Size {
                width: measure(
                    known.width,
                    space.width,
                    metrics.minimum.width,
                    metrics.preferred.width,
                ),
                height: measure(
                    known.height,
                    space.height,
                    metrics.minimum.height,
                    metrics.preferred.height,
                ),
            },
            Self::Ahem { segments, vertical } => {
                // Taffy 0.14 tests/common/src/lib.rs at the pinned upstream
                // commit: 10x10 Ahem glyphs, U+200B break opportunities.
                let (known_inline, known_block, available) = if *vertical {
                    (known.height, known.width, space.height)
                } else {
                    (known.width, known.height, space.width)
                };
                let minimum = *segments.iter().max().unwrap_or(&0) as f32 * 10.0;
                let maximum = segments.iter().sum::<usize>() as f32 * 10.0;
                let inline = known_inline
                    .unwrap_or_else(|| match available {
                        AvailableSpace::MinContent => minimum,
                        AvailableSpace::MaxContent => maximum,
                        AvailableSpace::Definite(size) => size.min(maximum),
                    })
                    .max(minimum);
                let capacity = (inline / 10.0).floor() as usize;
                let mut lines = 1;
                let mut current = 0;
                for length in segments {
                    if current + length > capacity {
                        if current > 0 {
                            lines += 1;
                        }
                        current = *length;
                    } else {
                        current += length;
                    }
                }
                let block = known_block.unwrap_or(lines as f32 * 10.0);
                let computed = if *vertical {
                    Size {
                        width: block,
                        height: inline,
                    }
                } else {
                    Size {
                        width: inline,
                        height: block,
                    }
                };
                Size {
                    width: known.width.unwrap_or(computed.width),
                    height: known.height.unwrap_or(computed.height),
                }
            }
        }
    }

    fn baseline(&self) -> Option<f32> {
        match self {
            Self::Metrics(metrics) => metrics.baseline,
            Self::Ahem { .. } => None,
        }
    }
}

fn metrics(input: &MetricsInput) -> Result<Metrics> {
    let [min_w, min_h] = input.minimum;
    let [pref_w, pref_h] = input.preferred;
    if [min_w, min_h, pref_w, pref_h]
        .into_iter()
        .any(|v| !v.is_finite() || !(0.0..=1_000_000.0).contains(&v))
        || min_w > pref_w
        || min_h > pref_h
        || input
            .baseline
            .is_some_and(|v| !v.is_finite() || v < 0.0 || v > pref_h)
    {
        return Err("Invalid leaf measurement".into());
    }
    Ok(Metrics {
        minimum: Size {
            width: min_w,
            height: min_h,
        },
        preferred: Size {
            width: pref_w,
            height: pref_h,
        },
        baseline: input.baseline,
    })
}

fn run_case(case: Case, allowed_fields: &[String], include_grid_details: bool) -> Result<Value> {
    if case.name.is_empty() || case.name.len() > 100 {
        return Err("Invalid case name".into());
    }
    if case.nodes.is_empty() || case.nodes.len() > 512 {
        return Err("Node count outside reference limits".into());
    }
    let mut ids = HashSet::new();
    let mut depths = Vec::with_capacity(case.nodes.len());
    let mut children = vec![Vec::new(); case.nodes.len()];
    for (index, node) in case.nodes.iter().enumerate() {
        if !ids.insert(node.id) {
            return Err("Duplicate node ID".into());
        }
        let depth = match (index, node.parent) {
            (0, None) => 0,
            (0, Some(_)) | (_, None) => return Err("Invalid root or detached node".into()),
            (_, Some(parent)) if parent < index => {
                children[parent].push(index);
                depths[parent] + 1
            }
            _ => return Err("Parent must precede child".into()),
        };
        if depth > 64 {
            return Err("Tree depth outside reference limits".into());
        }
        depths.push(depth);
    }
    // Validate measured nodes after all descendants are known.
    for (index, node) in case.nodes.iter().enumerate() {
        if node.measure.is_some() && !children[index].is_empty() {
            return Err("A measured node cannot have children".into());
        }
    }
    let [available_width, available_height] = case.available;
    let available = Size {
        width: available_width.into_taffy()?,
        height: available_height.into_taffy()?,
    };
    let mut tree: TaffyTree<LeafContent> = TaffyTree::new();
    tree.disable_rounding();
    let mut nodes = vec![None; case.nodes.len()];
    for index in (0..case.nodes.len()).rev() {
        let input = &case.nodes[index];
        let style = decode_style(&input.style, allowed_fields)
            .map_err(|error| format!("node {} Style: {error}", input.id))?;
        let node = if let Some(measurement) = &input.measure {
            tree.new_leaf_with_context(style, LeafContent::from_input(measurement)?)
        } else if children[index].is_empty() {
            tree.new_leaf(style)
        } else {
            let child_nodes: Vec<_> = children[index]
                .iter()
                .map(|&child| nodes[child].unwrap())
                .collect();
            tree.new_with_children(style, &child_nodes)
        }
        .map_err(|e| e.to_string())?;
        nodes[index] = Some(node);
    }
    let root = nodes[0].unwrap();
    tree.compute_layout_with_measure(root, available, |inputs, _, context, style| {
        let content = context.as_deref();
        let mut output = taffy::compute_leaf_layout(
            inputs,
            style,
            |_, _| 0.0,
            |known, space| {
                content.map_or_else(
                    || known.map(|value| value.unwrap_or(0.0)),
                    |content| content.measure(known, space),
                )
            },
        );
        output.baselines =
            taffy::tree::Baselines::from_first(content.and_then(LeafContent::baseline));
        output
    })
    .map_err(|e| e.to_string())?;
    let mut result = Vec::with_capacity(case.nodes.len());
    for (input, node) in case.nodes.iter().zip(nodes) {
        let layout = tree.layout(node.unwrap()).map_err(|e| e.to_string())?;
        let values = [
            layout.location.x,
            layout.location.y,
            layout.size.width,
            layout.size.height,
            layout.border.left,
            layout.border.top,
            layout.border.right,
            layout.border.bottom,
            layout.padding.left,
            layout.padding.top,
            layout.padding.right,
            layout.padding.bottom,
            layout.margin.left,
            layout.margin.top,
            layout.margin.right,
            layout.margin.bottom,
            layout.scrollbar_size.width,
            layout.scrollbar_size.height,
            layout.scrollable_overflow_rect.left,
            layout.scrollable_overflow_rect.top,
            layout.scrollable_overflow_rect.right,
            layout.scrollable_overflow_rect.bottom,
        ];
        if values.iter().any(|value| !value.is_finite()) {
            return Err(format!("Non-finite Taffy layout for node {}", input.id));
        }
        let mut node_result = json!({
            "id": input.id,
            "parent": input.parent,
            "rect": [layout.location.x, layout.location.y, layout.size.width, layout.size.height],
            "border": [layout.border.left, layout.border.top, layout.border.right, layout.border.bottom],
            "padding": [layout.padding.left, layout.padding.top, layout.padding.right, layout.padding.bottom],
            "margin": [layout.margin.left, layout.margin.top, layout.margin.right, layout.margin.bottom],
            "scrollbar": [layout.scrollbar_size.width, layout.scrollbar_size.height],
            "overflow": [layout.scrollable_overflow_rect.left, layout.scrollable_overflow_rect.top,
                layout.scrollable_overflow_rect.right, layout.scrollable_overflow_rect.bottom],
            "order": layout.order,
        });
        if include_grid_details {
            node_result["grid"] = match tree.detailed_layout_info(node.unwrap()) {
                taffy::tree::DetailedLayoutInfo::Grid(info) => {
                    let rows = &info.rows;
                    let columns = &info.columns;
                    if rows
                        .positions
                        .iter()
                        .chain(columns.positions.iter())
                        .any(|track| !track.start.is_finite() || !track.end.is_finite())
                    {
                        return Err(format!("Non-finite Taffy Grid track for node {}", input.id));
                    }
                    json!({
                        "rows": {
                            "negative": rows.negative_implicit_tracks,
                            "explicit": rows.explicit_tracks,
                            "positive": rows.positive_implicit_tracks,
                            "positions": rows.positions.iter()
                                .map(|track| [track.start, track.end]).collect::<Vec<_>>(),
                        },
                        "columns": {
                            "negative": columns.negative_implicit_tracks,
                            "explicit": columns.explicit_tracks,
                            "positive": columns.positive_implicit_tracks,
                            "positions": columns.positions.iter()
                                .map(|track| [track.start, track.end]).collect::<Vec<_>>(),
                        },
                        "items": info.items.iter().map(|item| [
                            item.row_start, item.row_end,
                            item.column_start, item.column_end,
                        ]).collect::<Vec<_>>(),
                    })
                }
                taffy::tree::DetailedLayoutInfo::None => Value::Null,
            };
        }
        result.push(node_result);
    }
    Ok(json!({"name": case.name, "nodes": result}))
}

pub(super) fn run(input: &str) -> Result<()> {
    if input.len() > 4 * 1024 * 1024 {
        return Err("Tree reference input too large".into());
    }
    let StrictValue(value) = serde_json::from_str(input).map_err(|e| e.to_string())?;
    if value.get("protocol").and_then(Value::as_str) == Some(FIELDS_PROTOCOL) {
        if value.as_object().is_none_or(|object| object.len() != 1) {
            return Err("Style inventory request has extra fields".into());
        }
        println!(
            "{}",
            json!({"protocol": FIELDS_PROTOCOL, "taffy": "0.14.0", "fields": style_fields()?})
        );
        return Ok(());
    }
    let document: Document = serde_json::from_value(value).map_err(|e| e.to_string())?;
    if !matches!(
        document.protocol.as_str(),
        TREE_PROTOCOL | GRID_DETAILS_PROTOCOL
    ) || document.cases.is_empty()
        || document.cases.len() > 1024
    {
        return Err("Invalid tree protocol or case count".into());
    }
    let include_grid_details = document.protocol == GRID_DETAILS_PROTOCOL;
    let fields = style_fields()?;
    let mut results = Vec::with_capacity(document.cases.len());
    let mut names = HashSet::new();
    for case in document.cases {
        if !names.insert(case.name.clone()) {
            return Err("Duplicate case name".into());
        }
        results.push(run_case(case, &fields, include_grid_details)?);
    }
    println!(
        "{}",
        json!({"protocol": document.protocol, "taffy": "0.14.0", "cases": results})
    );
    Ok(())
}
