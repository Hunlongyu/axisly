//! Semantic style input for the tree reference protocol.
use serde_json::{json, Value};
use taffy::prelude::*;
use taffy::style::{GridAutoTracks, GridTemplateTracks};

type Result<T> = std::result::Result<T, String>;

fn check_numbers(value: &Value) -> Result<()> {
    match value {
        Value::Number(number) => {
            let number = number.as_f64().ok_or("Invalid numeric style value")?;
            if !number.is_finite() || number.abs() > 1_000_000.0 {
                return Err("Numeric style value outside reference limits".into());
            }
        }
        Value::Array(array) => {
            for item in array {
                check_numbers(item)?;
            }
        }
        Value::Object(object) => {
            for item in object.values() {
                check_numbers(item)?;
            }
        }
        _ => {}
    }
    Ok(())
}

pub(super) fn style_fields() -> Result<Vec<String>> {
    let style = serde_json::to_value(Style::<String>::default()).map_err(|e| e.to_string())?;
    let mut fields: Vec<_> = style
        .as_object()
        .ok_or("Taffy Style did not serialize as an object")?
        .keys()
        .cloned()
        .collect();
    fields.sort();
    Ok(fields)
}

#[derive(Clone, Copy)]
enum LengthKind {
    Dimension,
    Auto,
    Plain,
}

fn length_value(value: &Value, kind: LengthKind) -> Result<Value> {
    if let Some(number) = value.as_f64() {
        let number = number as f32;
        if !number.is_finite() || number.abs() > 1_000_000.0 {
            return Err("Length outside reference limits".into());
        }
        return match kind {
            LengthKind::Dimension => serde_json::to_value(Dimension::length(number)),
            LengthKind::Auto => serde_json::to_value(LengthPercentageAuto::length(number)),
            LengthKind::Plain => serde_json::to_value(LengthPercentage::length(number)),
        }
        .map_err(|error| error.to_string());
    }
    let text = value
        .as_str()
        .ok_or("Length must be a number or CSS value string")?;
    if text.contains("calc(") {
        return Err("TaffyTree cannot resolve calc values; use the dedicated calc oracle".into());
    }
    match kind {
        LengthKind::Dimension => text
            .parse::<Dimension>()
            .map_err(|error| format!("{error:?}"))
            .and_then(|value| serde_json::to_value(value).map_err(|error| error.to_string())),
        LengthKind::Auto => text
            .parse::<LengthPercentageAuto>()
            .map_err(|error| format!("{error:?}"))
            .and_then(|value| serde_json::to_value(value).map_err(|error| error.to_string())),
        LengthKind::Plain => text
            .parse::<LengthPercentage>()
            .map_err(|error| format!("{error:?}"))
            .and_then(|value| serde_json::to_value(value).map_err(|error| error.to_string())),
    }
}

fn normalize_lengths(style: &mut Value) -> Result<()> {
    let object = style.as_object_mut().ok_or("Style must be an object")?;
    for (field, kind) in [
        ("size", LengthKind::Dimension),
        ("min_size", LengthKind::Auto),
        ("max_size", LengthKind::Auto),
        ("gap", LengthKind::Plain),
    ] {
        if let Some(value) = object.get_mut(field) {
            let pair = value
                .as_object_mut()
                .ok_or(format!("{field} must be an object"))?;
            if pair.keys().any(|key| key != "width" && key != "height") {
                return Err(format!("Unknown {field} axis"));
            }
            for axis in ["width", "height"] {
                let entry = pair.entry(axis).or_insert_with(|| match kind {
                    LengthKind::Dimension | LengthKind::Auto => json!("auto"),
                    LengthKind::Plain => json!(0),
                });
                *entry = length_value(entry, kind)?;
            }
        }
    }
    for (field, kind) in [
        ("inset", LengthKind::Auto),
        ("margin", LengthKind::Auto),
        ("padding", LengthKind::Plain),
        ("border", LengthKind::Plain),
    ] {
        if let Some(value) = object.get_mut(field) {
            let edges = value
                .as_object_mut()
                .ok_or(format!("{field} must be an object"))?;
            if edges
                .keys()
                .any(|key| !["left", "top", "right", "bottom"].contains(&key.as_str()))
            {
                return Err(format!("Unknown {field} edge"));
            }
            for side in ["left", "top", "right", "bottom"] {
                let entry = edges.entry(side).or_insert_with(|| match kind {
                    LengthKind::Auto if field == "inset" => json!("auto"),
                    _ => json!(0),
                });
                *entry = length_value(entry, kind)?;
            }
        }
    }
    if let Some(value) = object.get_mut("flex_basis") {
        *value = length_value(value, LengthKind::Dimension)?;
    }
    Ok(())
}

fn normalize_grid(style: &mut Value) -> Result<()> {
    let object = style.as_object_mut().ok_or("Style must be an object")?;
    for (field, names_field) in [
        ("grid_template_rows", "grid_template_row_names"),
        ("grid_template_columns", "grid_template_column_names"),
    ] {
        if let Some(value) = object.remove(field) {
            let text = value
                .as_str()
                .ok_or(format!("{field} must be a CSS track-list string"))?;
            let tracks: GridTemplateTracks<String, GridTemplateComponent<String>> =
                text.parse().map_err(|error| format!("{error:?}"))?;
            if object.contains_key(names_field) {
                return Err(format!("{field} already supplies {names_field}"));
            }
            object.insert(
                field.into(),
                serde_json::to_value(tracks.tracks).map_err(|error| error.to_string())?,
            );
            object.insert(
                names_field.into(),
                serde_json::to_value(tracks.line_names).map_err(|error| error.to_string())?,
            );
        }
    }
    for field in ["grid_auto_rows", "grid_auto_columns"] {
        if let Some(value) = object.remove(field) {
            let text = value
                .as_str()
                .ok_or(format!("{field} must be a CSS track-list string"))?;
            let tracks: GridAutoTracks = text.parse().map_err(|error| format!("{error:?}"))?;
            object.insert(
                field.into(),
                serde_json::to_value(tracks.0).map_err(|error| error.to_string())?,
            );
        }
    }
    for field in ["grid_row", "grid_column"] {
        if let Some(value) = object.remove(field) {
            let placement = value
                .as_object()
                .ok_or(format!("{field} must be an object"))?;
            if placement.keys().any(|key| key != "start" && key != "end") {
                return Err(format!("Unknown {field} placement key"));
            }
            let parse = |part: &str| -> Result<GridPlacement<String>> {
                part.parse().map_err(|error| format!("{error:?}"))
            };
            let start = parse(
                placement
                    .get("start")
                    .and_then(Value::as_str)
                    .unwrap_or("auto"),
            )?;
            let end = parse(
                placement
                    .get("end")
                    .and_then(Value::as_str)
                    .unwrap_or("auto"),
            )?;
            object.insert(
                field.into(),
                serde_json::to_value(Line { start, end }).map_err(|error| error.to_string())?,
            );
        }
    }
    Ok(())
}

pub(super) fn decode_style(input: &Value, allowed_fields: &[String]) -> Result<Style<String>> {
    let fields = input.as_object().ok_or("Style must be an object")?;
    for key in fields.keys() {
        if key == "dummy" || allowed_fields.binary_search(key).is_err() {
            return Err(format!("Unknown or non-layout Style field: {key}"));
        }
    }
    check_numbers(input)?;
    let mut normalized = input.clone();
    normalize_lengths(&mut normalized)?;
    normalize_grid(&mut normalized)?;
    serde_json::from_value(normalized).map_err(|error| error.to_string())
}
