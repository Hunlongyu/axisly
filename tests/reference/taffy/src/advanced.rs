//! The V2 subset uses Taffy's own diagnostic grid placement and track positions.
//! No probe nodes are inserted: probes would change automatic placement.
use super::*;
use taffy::tree::DetailedLayoutInfo;

fn position(reader: &mut Reader) -> Result<Option<u16>> {
    let value: u32 = reader.number()?;
    if value == u32::MAX {
        Ok(None)
    } else if value <= 15 {
        Ok(Some(value as u16))
    } else {
        Err("Invalid position".into())
    }
}

pub(super) fn run_case(reader: &mut Reader, index: u16) -> Result<()> {
    let available = reader.size()?;
    let padding = reader.edges()?;
    let gap = reader.size()?;
    let nc = reader.count(16, false)?;
    let nr = reader.count(16, false)?;
    let count = reader.count(128, true)?;
    let columns = reader.tracks(nc)?;
    let rows = reader.tracks(nr)?;
    let flow = reader.count(4, true)?;
    let implicit = reader.count(1, true)? != 0;
    let implicit_column = reader.unit()?;
    let implicit_row = reader.unit()?;
    let mut tree: TaffyTree<Metrics> = TaffyTree::new();
    tree.disable_rounding();
    let mut children = Vec::new();
    let mut ids = Vec::new();
    let mut unique = HashSet::new();
    for _ in 0..count {
        let id: u64 = reader.number()?;
        if !unique.insert(id) {
            return Err("Duplicate item ID".into());
        }
        let column = position(reader)?;
        let row = position(reader)?;
        let cs = reader.count(16, false)?;
        let rs = reader.count(16, false)?;
        if flow == 0 && (column.is_none() || row.is_none()) {
            return Err("Explicit flow requires positions".into());
        }
        let margin = reader.edges()?;
        let horizontal = alignment(reader.count(3, true)?);
        let vertical = reader.count(4, true)?;
        let minimum = reader.size()?;
        let preferred = reader.size()?;
        let baseline = if reader.count(1, true)? != 0 {
            Some(reader.unit()?)
        } else {
            None
        };
        if minimum.width > preferred.width
            || minimum.height > preferred.height
            || baseline.is_some_and(|b| b > preferred.height)
        {
            return Err("Invalid measurement ordering or baseline".into());
        }
        let mut style = placement(0, 0, cs, rs);
        style.grid_column.start = column.map_or_else(auto, |v| line((v + 1) as i16));
        style.grid_row.start = row.map_or_else(auto, |v| line((v + 1) as i16));
        style.margin = Rect {
            left: length(margin[0]),
            top: length(margin[1]),
            right: length(margin[2]),
            bottom: length(margin[3]),
        };
        style.justify_self = Some(horizontal);
        style.align_self = Some(if vertical == 4 {
            AlignItems::BASELINE
        } else {
            alignment(vertical)
        });
        children.push(
            tree.new_leaf_with_context(
                style,
                Metrics {
                    minimum,
                    preferred,
                    baseline,
                },
            )
            .map_err(|e| e.to_string())?,
        );
        ids.push(id);
    }
    let style = Style {
        display: Display::Grid,
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
        grid_template_columns: columns,
        grid_template_rows: rows,
        grid_auto_columns: vec![length(implicit_column)],
        grid_auto_rows: vec![length(implicit_row)],
        grid_auto_flow: [
            GridAutoFlow::Row,
            GridAutoFlow::Row,
            GridAutoFlow::Column,
            GridAutoFlow::RowDense,
            GridAutoFlow::ColumnDense,
        ][flow as usize],
        justify_content: Some(AlignContent::START),
        align_content: Some(AlignContent::START),
        ..Default::default()
    };
    let root = tree
        .new_with_children(style, &children)
        .map_err(|e| e.to_string())?;
    compute(&mut tree, root, available)?;
    let DetailedLayoutInfo::Grid(info) = tree.detailed_layout_info(root) else {
        return Err("Missing Taffy grid diagnostics".into());
    };
    if info.columns.negative_implicit_tracks != 0
        || info.rows.negative_implicit_tracks != 0
        || info.items.len() != children.len()
    {
        return Err("Unexpected placement diagnostic shape".into());
    }
    if !implicit
        && (info.columns.positive_implicit_tracks != 0 || info.rows.positive_implicit_tracks != 0)
    {
        return Err("Implicit tracks disabled for this case".into());
    }
    let width = info.columns.positions.last().map_or(padding[0], |p| p.end) + padding[2];
    let height = info.rows.positions.last().map_or(padding[1], |p| p.end) + padding[3];
    let mut output = Vec::new();
    for ((id, node), area) in ids.iter().zip(&children).zip(&info.items) {
        let col = (area.column_start - 1) as usize;
        let row = (area.row_start - 1) as usize;
        let first_x = &info.columns.positions[col];
        let last_x = &info.columns.positions[(area.column_end - 2) as usize];
        let first_y = &info.rows.positions[row];
        let last_y = &info.rows.positions[(area.row_end - 2) as usize];
        output.push(format!(
            "{{\"id\":{},\"rect\":{},\"cell\":[{},{},{},{}],\"at\":[{},{},{},{}]}}",
            id,
            rect(tree.layout(*node).map_err(|e| e.to_string())?),
            first_x.start,
            first_y.start,
            last_x.end - first_x.start,
            last_y.end - first_y.start,
            col,
            row,
            area.column_end - area.column_start,
            area.row_end - area.row_start
        ));
    }
    println!(
        "{{\"case\":{},\"size\":[{},{}],\"items\":[{}]}}",
        index,
        width,
        height,
        output.join(",")
    );
    Ok(())
}
