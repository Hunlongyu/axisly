"""Validate and compare every assertion stored in the pinned XML fixtures."""

import math
import re
import struct


GEOMETRY_FIELDS = ("x", "y", "width", "height")
METRIC_FIELDS = ("scroll_width", "scroll_height", "resolved-rows", "resolved-columns")


def parse_resolved_tracks(value):
    if value.strip() == "none":
        return []
    tokens = []
    for match in re.finditer(r"\[([^\]]*)\]|([^\s]+)", value):
        if match[1] is not None:
            names = match[1].split()
            if not names:
                raise ValueError("Empty resolved track name group")
            tokens.append(names)
        else:
            token = match[2]
            if not token.endswith("px"):
                raise ValueError(f"Invalid resolved track size {token!r}")
            size = float(token[:-2])
            if not math.isfinite(size) or size < 0:
                raise ValueError("Invalid resolved track size")
            tokens.append(size)
    if not tokens:
        raise ValueError("Empty resolved track list")
    return tokens


def validate_expectation(expected):
    unknown = expected.keys() - set(GEOMETRY_FIELDS + METRIC_FIELDS)
    if unknown:
        raise ValueError(f"Unknown upstream expectation {sorted(unknown)}")
    if not set(GEOMETRY_FIELDS) <= expected.keys():
        raise ValueError("Incomplete upstream geometry expectation")
    for key, value in expected.items():
        if key.startswith("resolved-"):
            parse_resolved_tracks(value)
        elif not math.isfinite(float(value)):
            raise ValueError("Non-finite upstream expectation")


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def round_pixel(value):
    return math.floor(value + 0.5) if value >= 0 else math.ceil(value - 0.5)


def observed_layouts(case, result, absolute):
    cumulative = []
    for index, source in enumerate(result["nodes"]):
        node = {key: list(value) for key, value in source.items()
                if key in ("rect", "border", "scrollbar", "overflow")}
        rect = [f32(value) for value in node["rect"]]
        parent = case["nodes"][index]["parent"]
        if absolute:
            cumulative.append(rect[:2])
            if parent is not None:
                rect[0] = f32(rect[0] - f32(result["nodes"][parent]["rect"][0]))
                rect[1] = f32(rect[1] - f32(result["nodes"][parent]["rect"][1]))
        else:
            cumulative.append([f32(rect[0] + (cumulative[parent][0] if parent is not None else 0)),
                               f32(rect[1] + (cumulative[parent][1] if parent is not None else 0))])
        if case["use_rounding"]:
            x, y = cumulative[-1]
            width, height = rect[2:]
            rect = [round_pixel(rect[0]), round_pixel(rect[1]),
                    round_pixel(f32(x + width)) - round_pixel(x),
                    round_pixel(f32(y + height)) - round_pixel(y)]
            if "border" in node:
                left, top, right, bottom = node["border"]
                node["border"] = [round_pixel(f32(x + left)) - round_pixel(x),
                    round_pixel(f32(y + top)) - round_pixel(y),
                    round_pixel(f32(x + width)) - round_pixel(f32(f32(x + width) - right)),
                    round_pixel(f32(y + height)) - round_pixel(f32(f32(y + height) - bottom))]
            if "overflow" in node:
                node["overflow"] = [round_pixel(f32(origin + edge)) - round_pixel(origin)
                    for origin, edge in zip((x, y, x, y), node["overflow"], strict=True)]
            if "scrollbar" in node:
                node["scrollbar"] = [round_pixel(edge) for edge in node["scrollbar"]]
        node["rect"] = rect
        yield node


def grid_tokens(axis):
    names = axis["line_names"]
    positions = axis["positions"]
    if not positions:
        return []
    tokens = []
    for index, (start, end) in enumerate(positions):
        if names and names[index]:
            tokens.append(names[index])
        tokens.append(f32(f32(end) - f32(start)))
    if names and names[-1]:
        tokens.append(names[-1])
    return tokens


def xml_differences(case, result, absolute):
    if "error" in result:
        return [], []
    geometry, metrics = [], []
    for index, (source, node, expected) in enumerate(zip(result["nodes"],
            observed_layouts(case, result, absolute), case["xml_expected"], strict=True)):
        target = [float(expected[key]) for key in GEOMETRY_FIELDS]
        if any(abs(a - b) >= 0.1 for a, b in zip(node["rect"], target, strict=True)):
            geometry.append({"node": index + 1, "field": "rect",
                             "actual": node["rect"], "expected": target})
        for axis, key in enumerate(("scroll_width", "scroll_height")):
            if key not in expected:
                continue
            # Taffy Layout::scroll_width/height report maximum offsets, rather
            # than CSSOM's total scrollWidth/scrollHeight dimensions.
            size = node["rect"][axis + 2]
            actual = max(0, f32(f32(f32(f32(node["overflow"][axis + 2]
                + min(node["scrollbar"][axis], size)) - size)
                + node["border"][axis]) + node["border"][axis + 2]))
            if abs(actual - float(expected[key])) >= 0.1:
                metrics.append({"node": index + 1, "field": key,
                                "actual": actual, "expected": float(expected[key])})
        for axis in ("rows", "columns"):
            key = "resolved-" + axis
            if key not in expected:
                continue
            actual = grid_tokens(source["grid"][axis]) if source["grid"] else []
            target = parse_resolved_tracks(expected[key])
            if (len(actual) != len(target)
                    or any(a != b if isinstance(a, list) or isinstance(b, list)
                           else abs(a - b) >= 0.1 for a, b in zip(actual, target))):
                metrics.append({"node": index + 1, "field": key,
                                "actual": actual, "expected": target})
    return geometry, metrics
