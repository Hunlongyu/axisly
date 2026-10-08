"""Emit typed C++ fixtures from the development tree protocol.

The production library receives no parser dependency. This adapter rejects
unknown fields and malformed tokens rather than silently dropping test input.
"""

import argparse
import json
import itertools
import math
from pathlib import Path
import re
import struct


def number(value):
    numeric = float(value)
    if not math.isfinite(numeric) or abs(numeric) > 3.4028234663852886e38:
        raise ValueError(f"Numeric fixture value out of range: {value!r}")
    numeric = struct.unpack("f", struct.pack("f", numeric))[0]
    text = format(numeric, ".9g")
    return text + ("F" if "." in text or "e" in text else ".0F")


def string(value):
    # Fixed-width octal escapes preserve UTF-8 bytes independently of the
    # compiler's execution character set and adjacent hexadecimal characters.
    result = []
    for byte in value.encode("utf-8"):
        if byte in (34, 92):
            result.append("\\" + chr(byte))
        elif 32 <= byte < 127:
            result.append(chr(byte))
        else:
            result.append(f"\\{byte:03o}")
    return '"' + "".join(result) + '"'


def split(value, commas=False):
    tokens, current = [], []
    stack = []
    closing = {")": "(", "]": "["}
    for char in value.strip():
        if char in "([":
            stack.append(char)
        elif char in ")]":
            if not stack or stack.pop() != closing[char]:
                raise ValueError(f"Unbalanced fixture token {value!r}")
        delimiter = char == "," if commas else char.isspace()
        if delimiter and not stack:
            if current:
                tokens.append("".join(current).strip())
                current = []
        else:
            current.append(char)
    if stack:
        raise ValueError(f"Unbalanced fixture token {value!r}")
    if current:
        tokens.append("".join(current).strip())
    return tokens


def numeric_length(value):
    match = re.fullmatch(r"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)(px|%)?", value)
    if not match:
        raise ValueError(f"Unsupported numeric length {value!r}")
    numeric = float(match[1])
    if match[2] == "%":
        numeric = struct.unpack("f", struct.pack("f", numeric))[0] / 100
        return "percent", number(numeric)
    return "length", number(numeric)


def length(value, prefix="Length"):
    if isinstance(value, (int, float)):
        return f"{prefix}::length({number(value)})"
    methods = {"auto": "automatic", "min-content": "min_content",
               "max-content": "max_content", "fit-content": "fit_content",
               "stretch": "stretch", "content": "content"}
    if value in methods:
        return f"{prefix}::{methods[value]}()"
    fit = re.fullmatch(r"fit-content\((.+)\)", value)
    if fit:
        kind, numeric = numeric_length(fit[1].strip())
        return f"{prefix}::fit_content_{kind}({numeric})"
    kind, numeric = numeric_length(value)
    return f"{prefix}::{kind}({numeric})"


def track(value, breadth=False):
    prefix = "TrackBreadth" if breadth else "TrackSizing"
    if value.startswith("minmax(") and value.endswith(")") and not breadth:
        parts = split(value[7:-1], commas=True)
        if len(parts) != 2:
            raise ValueError(f"Invalid minmax fixture {value!r}")
        return f"TrackSizing::minmax({track(parts[0], True)}, {track(parts[1], True)})"
    fraction = re.fullmatch(r"((?:\d+(?:\.\d*)?|\.\d+))fr", value)
    if fraction:
        return f"{prefix}::fraction({number(fraction[1])})"
    if value in ("auto", "min-content", "max-content") or value.startswith("fit-content("):
        return length(value, prefix)
    kind, numeric = numeric_length(value)
    method = "fixed" if kind == "length" and not breadth else kind
    return f"{prefix}::{method}({numeric})"


def strings(values):
    return "{" + ", ".join(string(value) for value in values) + "}"


def names(values):
    return "{" + ", ".join(strings(value) for value in values) + "}"


def track_list(value, allow_repeat=True):
    if value in ("none", ""):
        return "{}", "{}"
    components, line_names = [], [[]]
    for token in split(value):
        if token.startswith("[") and token.endswith("]"):
            line_names[-1].extend(token[1:-1].split())
            continue
        if token.startswith("repeat(") and token.endswith(")"):
            if not allow_repeat:
                raise ValueError("Repeated automatic tracks are invalid")
            parts = split(token[7:-1], commas=True)
            if len(parts) != 2:
                raise ValueError(f"Invalid repeat fixture {token!r}")
            if parts[0] in ("auto-fill", "auto-fit"):
                repeat_kind = "AutoFill" if parts[0] == "auto-fill" else "AutoFit"
                count = 1
            else:
                repeat_kind = "Count"
                count = int(parts[0])
                if not 1 <= count <= 65535:
                    raise ValueError("Repeat count out of range")
            inner, inner_names = track_list(parts[1], False)
            components.append(f"GridRepeat{{RepeatKind::{repeat_kind}, {count}, {inner}, {inner_names}}}")
        else:
            components.append(track(token))
        line_names.append([])
    if len(components) > 256:
        runs = [(sum(1 for _ in entries), expression)
                for expression, entries in itertools.groupby(components)]
        kind = "TemplateTrack" if allow_repeat else "TrackSizing"
        tracks = f"fixture_track_runs<{kind}>({{" + ", ".join(
            f"{{{count}, {expression}}}" for count, expression in runs) + "})"
    else:
        tracks = "{" + ", ".join(components) + "}"
    return tracks, names(line_names) if any(line_names) else "{}"


def grid_line(value):
    if value == "auto":
        return "GridLine::automatic()"
    parts = value.split()
    span = "span" in parts
    if span:
        parts.remove("span")
    integers = [int(part) for part in parts if re.fullmatch(r"[+-]?\d+", part)]
    identifiers = [part for part in parts if not re.fullmatch(r"[+-]?\d+", part)]
    if len(integers) > 1 or len(identifiers) > 1 or not parts:
        raise ValueError(f"Invalid Grid line {value!r}")
    count = integers[0] if integers else 1
    if span:
        if not 1 <= count <= 65535:
            raise ValueError("Grid span out of range")
        return (f"GridLine::named_span({string(identifiers[0])}, {count})" if identifiers
                else f"GridLine::span({count})")
    if not -32768 <= count <= 32767 or count == 0:
        raise ValueError("Grid line index out of range")
    return (f"GridLine::named({string(identifiers[0])}, {count})" if identifiers
            else f"GridLine::index({count})")


ENUM_FIELDS = {
    "display": ("display", "Display", ("Block", "FlowRoot", "Flex", "Grid", "None")),
    "direction": ("box.direction", "Direction", ("Ltr", "Rtl")),
    "box_sizing": ("box.box_sizing", "BoxSizing", ("BorderBox", "ContentBox")),
    "position": ("position", "Position", ("Relative", "Absolute")),
    "contain": ("contain", "Contain", ("None", "Layout", "Paint", "Content")),
    "float": ("float_side", "FloatSide", ("None", "Left", "Right")),
    "clear": ("clear", "ClearSide", ("None", "Left", "Right", "Both")),
    "text_align": ("text_align", "TextAlign", ("Auto", "LegacyLeft", "LegacyRight", "LegacyCenter")),
    "flex_direction": ("flex_direction", "FlexDirection", ("Row", "RowReverse", "Column", "ColumnReverse")),
    "flex_wrap": ("flex_wrap", "FlexWrap", ("NoWrap", "Wrap", "WrapReverse", "Balance", "BalanceReverse")),
    "grid_auto_flow": ("grid.auto_flow", "GridAutoFlow", ("Row", "Column", "RowDense", "ColumnDense")),
}
ITEM_POSITIONS = ("Start", "End", "FlexStart", "FlexEnd", "SelfStart", "SelfEnd",
                  "Center", "Baseline", "Stretch")
CONTENT_POSITIONS = ("Start", "End", "FlexStart", "FlexEnd", "Center", "Stretch",
                     "SpaceBetween", "SpaceEvenly", "SpaceAround")


def style_statements(style, variable):
    result = []

    def set_value(field, expression):
        result.append(f"    {variable}.{field} = {expression};")

    for field, value in style.items():
        if field == "contain" and isinstance(value, int) and not isinstance(value, bool):
            if not 0 <= value <= 3:
                raise ValueError("Containment bitset out of range")
            value = ("None", "Layout", "Paint", "Content")[value]
        if field in ENUM_FIELDS:
            target, kind, allowed = ENUM_FIELDS[field]
            if value not in allowed:
                raise ValueError(f"Unknown {field} value {value!r}")
            set_value(target, f"{kind}::{value}")
        elif field in ("size", "min_size", "max_size", "gap"):
            if set(value) - {"width", "height"}:
                raise ValueError(f"Unknown {field} axis")
            for axis, part in value.items():
                set_value(("" if field == "gap" else "box.") + field + "." + axis, length(part))
        elif field in ("margin", "padding", "border", "inset"):
            if set(value) - {"left", "top", "right", "bottom"}:
                raise ValueError(f"Unknown {field} edge")
            for edge, part in value.items():
                set_value(("" if field == "inset" else "box.") + field + "." + edge, length(part))
        elif field in ("aspect_ratio", "flex_grow", "flex_shrink", "scrollbar_width"):
            set_value(("box." if field == "aspect_ratio" else "") + field, number(value))
        elif field == "flex_line_count":
            if not isinstance(value, int) or not 1 <= value <= 65535:
                raise ValueError("Flex line count out of range")
            set_value(field, str(value))
        elif field == "flex_basis":
            set_value(field, length(value))
        elif field in ("item_is_replaced", "item_is_table"):
            if not isinstance(value, bool):
                raise ValueError("Item role must be boolean")
            set_value("box.replaced" if field == "item_is_replaced" else field,
                      "true" if value else "false")
        elif field == "overflow":
            if set(value) - {"x", "y"}:
                raise ValueError("Unknown overflow axis")
            for axis, part in value.items():
                if part not in ("Visible", "Clip", "Hidden", "Scroll"):
                    raise ValueError("Unknown overflow value")
                set_value(f"overflow_{axis}", f"Overflow::{part}")
        elif field in ("align_items", "align_self", "justify_items", "justify_self",
                       "align_content", "justify_content"):
            safety = "Safe" if value.startswith("Safe") else "Unsafe"
            position = value.removeprefix("Safe")
            content = field.endswith("content")
            if position not in (CONTENT_POSITIONS if content else ITEM_POSITIONS):
                raise ValueError(f"Unknown {field} value {value!r}")
            kind = "Content" if content else "Item"
            set_value(field, f"{kind}Alignment{{{kind}Position::{position}, AlignmentSafety::{safety}}}")
        elif field in ("grid_template_rows", "grid_template_columns", "grid_auto_rows", "grid_auto_columns"):
            template = "template" in field
            tracks, line_names = track_list(value, template)
            set_value("grid." + field.removeprefix("grid_"), tracks)
            if template and line_names != "{}":
                axis = "row" if field.endswith("rows") else "column"
                set_value(f"grid.{axis}_line_names", line_names)
        elif field in ("grid_row", "grid_column"):
            if set(value) - {"start", "end"}:
                raise ValueError("Unknown Grid endpoint")
            for endpoint, part in value.items():
                set_value("grid." + field.removeprefix("grid_") + "." + endpoint, grid_line(part))
        else:
            raise ValueError(f"Unmapped fixture Style field {field!r}")
    return result


def space(value):
    if value == "min-content":
        return "AvailableSpace::min_content()"
    if value == "max-content":
        return "AvailableSpace::max_content()"
    return f"AvailableSpace::definite({number(value)})"


def case_source(case, index):
    lines = [f"void fixture_{index}(std::ostream& out) {{", "    LayoutTree tree;"]
    for node_index, node in enumerate(case["nodes"]):
        lines.append(f"    Style style_{node_index};")
        lines.extend(style_statements(node["style"], f"style_{node_index}"))
        callback = ""
        if "measure" in node:
            measurement = node["measure"]
            if set(measurement) != {"ahem_text", "vertical"}:
                raise ValueError("This corpus requires explicit Ahem measurement data")
            callback = f", ahem_measurement({string(measurement['ahem_text'])}, {'true' if measurement['vertical'] else 'false'})"
        lines.append(f"    const auto node_{node_index} = tree.create_node(style_{node_index}{callback});")
        if node["parent"] is not None:
            if not 0 <= node["parent"] < node_index:
                raise ValueError("Parent must precede child")
            lines.append(f"    tree.append_child(node_{node['parent']}, node_{node_index});")
    identified = ", ".join(f"{{{node['id']}, node_{index}}}" for index, node in enumerate(case["nodes"]))
    available = ", ".join(space(value) for value in case["available"])
    lines.append(f"    reference::write_case(out, {string(case['name'])}, compute_fixture(tree, node_0, {{{available}}}), {{{identified}}}, true);")
    lines.append("}")
    return "\n".join(lines)


def write_if_changed(path, content):
    if not path.exists() or path.read_text(encoding="utf-8") != content:
        path.write_text(content, encoding="utf-8", newline="\n")


def generate(document, output, shard_size=100):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    cases = document["cases"]
    sources = []
    for start in range(0, len(cases), shard_size):
        shard = start // shard_size
        entries = cases[start:start + shard_size]
        lines = ['#include "upstream_fixture_runner.hpp"', "", "using namespace axisly;",
                 "using namespace axisly::css;", "using namespace axisly::css::reference;", "namespace {"]
        for offset, case in enumerate(entries):
            try:
                lines.append(case_source(case, start + offset))
            except (ValueError, KeyError, TypeError) as error:
                raise ValueError(f"{case.get('source', case['name'])}: {error}") from error
        lines.extend(["}", f"std::span<const UpstreamFixture> upstream_shard_{shard}() {{",
                      f"    static const std::array<UpstreamFixture, {len(entries)}> fixtures{{{{"])
        lines.extend(f"        {{{string(case['name'])}, fixture_{start + offset}}}," for offset, case in enumerate(entries))
        lines.extend(["    }};", "    return fixtures;", "}", ""])
        path = output / f"upstream_{shard:03d}.cpp"
        write_if_changed(path, "\n".join(lines))
        sources.append(path.name)
    registry = ['#include "upstream_fixture_runner.hpp"', ""]
    registry.extend(f"std::span<const UpstreamFixture> upstream_shard_{index}();" for index in range(len(sources)))
    registry.extend(["", "std::vector<UpstreamFixture> upstream_fixtures() {",
                     "    std::vector<UpstreamFixture> fixtures;", f"    fixtures.reserve({len(cases)});"])
    for index in range(len(sources)):
        registry.extend([f"    const auto shard_{index} = upstream_shard_{index}();",
                         f"    fixtures.insert(fixtures.end(), shard_{index}.begin(), shard_{index}.end());"])
    registry.extend(["    return fixtures;", "}", ""])
    write_if_changed(output / "upstream_registry.cpp", "\n".join(registry))
    sources.append("upstream_registry.cpp")
    cmake = ["set(axisly_upstream_generated_sources"]
    cmake.extend(f'    "${{CMAKE_CURRENT_LIST_DIR}}/{name}"' for name in sources)
    cmake.append(")")
    write_if_changed(output / "sources.cmake", "\n".join(cmake) + "\n")
    return len(sources)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    document = json.loads(Path(args.cases).read_text(encoding="utf-8"))
    count = generate(document, args.output)
    print(f"Generated {len(document['cases'])} typed fixtures in {count} sources")


if __name__ == "__main__":
    main()
