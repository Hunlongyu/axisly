use std::{
    collections::HashSet,
    io::{self, Read},
    str::{FromStr, SplitWhitespace},
};
use taffy::prelude::*;

type Result<T> = std::result::Result<T, String>;

struct Reader<'a>(SplitWhitespace<'a>);
impl Reader<'_> {
    fn token(&mut self) -> Result<&str> {
        self.0.next().ok_or_else(|| "Truncated input".into())
    }
    fn number<T: FromStr>(&mut self) -> Result<T> {
        self.token()?
            .parse()
            .map_err(|_| "Invalid numeric token".into())
    }
    fn unit(&mut self) -> Result<f32> {
        let value: f32 = self.number()?;
        if !value.is_finite() || !(0.0..=1_000_000.0).contains(&value) {
            return Err("Numeric input outside reference limits".into());
        }
        Ok(value)
    }
    fn count(&mut self, max: u16, allow_zero: bool) -> Result<u16> {
        let value = self.number()?;
        if value > max || (!allow_zero && value == 0) {
            return Err("Count outside reference limits".into());
        }
        Ok(value)
    }
    fn size(&mut self) -> Result<Size<f32>> {
        Ok(Size {
            width: self.unit()?,
            height: self.unit()?,
        })
    }
    fn edges(&mut self) -> Result<[f32; 4]> {
        Ok([self.unit()?, self.unit()?, self.unit()?, self.unit()?])
    }
    fn tracks(&mut self, count: u16) -> Result<Vec<GridTemplateComponent<String>>> {
        (0..count)
            .map(|_| match self.token()? {
                "F" => Ok(length(self.unit()?)),
                "C" => Ok(minmax(min_content(), max_content())),
                "W" | "R" => {
                    let weight = self.unit()?;
                    let minimum = self.unit()?;
                    if weight < 1.0 {
                        return Err("Reference weights must be at least one".into());
                    }
                    Ok(minmax(length(minimum), fr(weight)))
                }
                _ => Err("Unknown track token".into()),
            })
            .collect()
    }
}

#[derive(Clone, Copy, Default)]
struct Metrics {
    minimum: Size<f32>,
    preferred: Size<f32>,
}

fn alignment(value: u16) -> AlignItems {
    [
        AlignItems::START,
        AlignItems::CENTER,
        AlignItems::END,
        AlignItems::STRETCH,
    ][value as usize]
}

fn placement(column: u16, row: u16, columns: u16, rows: u16) -> Style {
    Style {
        grid_column: Line {
            start: line((column + 1) as i16),
            end: span(columns),
        },
        grid_row: Line {
            start: line((row + 1) as i16),
            end: span(rows),
        },
        min_size: Size {
            width: length(0.0),
            height: length(0.0),
        },
        justify_self: Some(AlignItems::STRETCH),
        align_self: Some(AlignItems::STRETCH),
        ..Default::default()
    }
}

fn measure(known: Option<f32>, available: AvailableSpace, minimum: f32, preferred: f32) -> f32 {
    known.unwrap_or(match available {
        AvailableSpace::MinContent => minimum,
        // Fixtures provide constant intrinsic metrics, just like the native
        // callback. Available space is advisory, not another clipping operation.
        AvailableSpace::MaxContent | AvailableSpace::Definite(_) => preferred,
    })
}

fn rect(layout: &Layout) -> String {
    format!(
        "[{},{},{},{}]",
        layout.location.x, layout.location.y, layout.size.width, layout.size.height
    )
}

fn compute(tree: &mut TaffyTree<Metrics>, root: NodeId, available: Size<f32>) -> Result<()> {
    tree.compute_layout_with_measure(
        root,
        Size {
            width: AvailableSpace::Definite(available.width),
            height: AvailableSpace::Definite(available.height),
        },
        |inputs, _, context, style| {
            let metrics = context.copied().unwrap_or_default();
            taffy::compute_leaf_layout(
                inputs,
                style,
                |_, _| 0.0,
                |known, space| Size {
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
            )
        },
    )
    .map_err(|e| e.to_string())
}

fn same_rect(a: &Layout, b: &Layout) -> bool {
    [a.location.x, a.location.y, a.size.width, a.size.height]
        .into_iter()
        .zip([b.location.x, b.location.y, b.size.width, b.size.height])
        .all(|(a, b)| {
            a.is_finite() && b.is_finite() && (a - b).abs() <= 1e-4 + 1e-6 * a.abs().max(b.abs())
        })
}

fn run_case(reader: &mut Reader, index: u16) -> Result<()> {
    let available = reader.size()?;
    let padding = reader.edges()?;
    let gap = reader.size()?;
    let columns = reader.count(16, false)?;
    let rows = reader.count(16, false)?;
    let count = reader.count(128, true)?;
    let mut root_style = Style {
        display: Display::Grid,
        box_sizing: BoxSizing::BorderBox,
        size: Size {
            width: length(available.width),
            height: length(available.height),
        },
        padding: Rect {
            left: length(padding[0]),
            top: length(padding[1]),
            right: length(padding[2]),
            bottom: length(padding[3]),
        },
        gap: Size {
            width: length(gap.width),
            height: length(gap.height),
        },
        justify_content: Some(AlignContent::START),
        align_content: Some(AlignContent::START),
        ..Default::default()
    };
    root_style.grid_template_columns = reader.tracks(columns)?;
    root_style.grid_template_rows = reader.tracks(rows)?;
    let mut tree: TaffyTree<Metrics> = TaffyTree::new();
    tree.disable_rounding();
    let mut children = Vec::new();
    let mut output = Vec::new();
    let mut ids = HashSet::new();
    for _ in 0..count {
        let id: u64 = reader.number()?;
        if !ids.insert(id) {
            return Err("Duplicate item ID".into());
        }
        let column = reader.count(15, true)?;
        let row = reader.count(15, true)?;
        let column_span = reader.count(16, false)?;
        let row_span = reader.count(16, false)?;
        if column + column_span > columns || row + row_span > rows {
            return Err("Invalid placement or span".into());
        }
        let margin = reader.edges()?;
        let horizontal = alignment(reader.count(3, true)?);
        let vertical = alignment(reader.count(3, true)?);
        let metrics = Metrics {
            minimum: reader.size()?,
            preferred: reader.size()?,
        };
        if metrics.minimum.width > metrics.preferred.width
            || metrics.minimum.height > metrics.preferred.height
        {
            return Err("Invalid measurement ordering".into());
        }
        let mut style = placement(column, row, column_span, row_span);
        style.margin = Rect {
            left: length(margin[0]),
            top: length(margin[1]),
            right: length(margin[2]),
            bottom: length(margin[3]),
        };
        style.justify_self = Some(horizontal);
        style.align_self = Some(vertical);
        let node = tree
            .new_leaf_with_context(style, metrics)
            .map_err(|e| e.to_string())?;
        // Zero-intrinsic stretch probes expose the full grid area without reimplementing
        // Taffy's track sizing. They add no contribution in the enforced common subset.
        let probe = tree
            .new_leaf_with_context(
                placement(column, row, column_span, row_span),
                Metrics::default(),
            )
            .map_err(|e| e.to_string())?;
        children.push(node);
        output.push((id, node, probe));
    }
    let extent = tree
        .new_leaf_with_context(placement(0, 0, columns, rows), Metrics::default())
        .map_err(|e| e.to_string())?;
    let root = tree
        .new_with_children(root_style, &children)
        .map_err(|e| e.to_string())?;
    compute(&mut tree, root, available)?;
    let baseline: Vec<_> = output
        .iter()
        .map(|(_, node, _)| tree.layout(*node).cloned())
        .collect::<std::result::Result<_, _>>()
        .map_err(|e| e.to_string())?;
    for (_, _, probe) in &output {
        tree.add_child(root, *probe).map_err(|e| e.to_string())?;
    }
    tree.add_child(root, extent).map_err(|e| e.to_string())?;
    compute(&mut tree, root, available)?;
    let occupied = tree.layout(extent).map_err(|e| e.to_string())?.size;
    let mut items = Vec::new();
    for ((id, node, probe), original) in output.into_iter().zip(baseline) {
        if !same_rect(&original, tree.layout(node).map_err(|e| e.to_string())?) {
            return Err("Reference probes changed an original item's layout".into());
        }
        items.push(format!(
            "{{\"id\":{},\"rect\":{},\"cell\":{}}}",
            id,
            rect(&original),
            rect(tree.layout(probe).map_err(|e| e.to_string())?)
        ));
    }
    println!(
        "{{\"case\":{},\"size\":[{},{}],\"items\":[{}]}}",
        index,
        occupied.width + padding[0] + padding[2],
        occupied.height + padding[1] + padding[3],
        items.join(",")
    );
    Ok(())
}

fn run() -> Result<()> {
    let mut input = String::new();
    io::stdin()
        .read_to_string(&mut input)
        .map_err(|e| e.to_string())?;
    let mut reader = Reader(input.split_whitespace());
    if reader.token()? != "GRIDLY_REFERENCE_V1" {
        return Err("Unsupported protocol version".into());
    }
    let count = reader.count(10000, false)?;
    for index in 0..count {
        run_case(&mut reader, index).map_err(|e| format!("taffy reference case {index}: {e}"))?;
    }
    if reader.0.next().is_some() {
        return Err("Unexpected trailing input".into());
    }
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}
