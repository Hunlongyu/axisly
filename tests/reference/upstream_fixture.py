"""Translate the pinned Taffy XML corpus into explicit tree-oracle inputs.

This is a development fixture adapter. Unknown attributes are errors; source
paths, hashes and browser-generated expectations remain attached to every case.
"""

import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

from upstream_expectations import validate_expectation


TAFFY_COMMIT = "77f385683c1d698c91a23a259f87fdddf26925fb"
ARCHIVE_SHA256 = "baf36d0a3f896d901b81c241b332e2fc59f2f1b6ab05c574b8323b29e35d0baa"
CORPUS_SHA256 = "833248fad307ab882325a9d3cd3650a94a187ab817abc424f7331aff846eec54"

ENUMS = {
    "display": {"block": "Block", "flow-root": "FlowRoot", "flex": "Flex",
                "grid": "Grid", "none": "None"},
    "direction": {"ltr": "Ltr", "rtl": "Rtl"},
    "box-sizing": {"border-box": "BorderBox", "content-box": "ContentBox"},
    "position": {"relative": "Relative", "absolute": "Absolute"},
    # Taffy serializes its containment bitset as u8, unlike its enums.
    "contain": {"none": 0, "layout": 1, "paint": 2, "content": 3},
    "float": {"none": "None", "left": "Left", "right": "Right"},
    "clear": {"none": "None", "left": "Left", "right": "Right", "both": "Both"},
    "flex-direction": {"row": "Row", "row-reverse": "RowReverse",
                       "column": "Column", "column-reverse": "ColumnReverse"},
    "flex-wrap": {"nowrap": "NoWrap", "wrap": "Wrap", "wrap-reverse": "WrapReverse",
                  "balance": "Balance", "wrap-reverse balance": "BalanceReverse"},
    "grid-auto-flow": {"row": "Row", "column": "Column", "row dense": "RowDense",
                       "column dense": "ColumnDense"},
    "text-align": {"auto": "Auto", "-webkit-left": "LegacyLeft",
                   "-webkit-right": "LegacyRight", "-webkit-center": "LegacyCenter"},
}
OVERFLOWS = {name: name.title() for name in ("visible", "clip", "hidden", "scroll")}
ALIGNMENTS = {name: "".join(part.title() for part in name.split("-")) for name in (
    "start", "end", "flex-start", "flex-end", "self-start", "self-end", "center",
    "baseline", "stretch", "space-between", "space-around", "space-evenly")}


def alignment(value):
    parts = value.split()
    if len(parts) == 1:
        return ALIGNMENTS[parts[0]]
    if len(parts) == 2 and parts[0] in ("safe", "unsafe"):
        return ("Safe" if parts[0] == "safe" else "") + ALIGNMENTS[parts[1]]
    raise ValueError(f"Unsupported alignment {value!r}")


def style(element):
    result = {}
    for key, value in element.attrib.items():
        if key in ENUMS:
            result[key.replace("-", "_")] = ENUMS[key][value]
        elif key in ("overflow-x", "overflow-y"):
            result.setdefault("overflow", {"x": "Visible", "y": "Visible"})[key[-1]] = OVERFLOWS[value]
        elif key in ("width", "height", "min-width", "min-height", "max-width", "max-height"):
            field, axis = ("size", key) if "-" not in key else key.split("-")
            if field != "size":
                field += "_size"
            result.setdefault(field, {})[axis] = value
        elif key in ("left", "top", "right", "bottom"):
            result.setdefault("inset", {})[key] = value
        elif re.fullmatch(r"(?:margin|padding|border)-(?:left|top|right|bottom)", key):
            field, edge = key.split("-")
            result.setdefault(field, {})[edge] = value
        elif key in ("row-gap", "column-gap"):
            result.setdefault("gap", {})["height" if key == "row-gap" else "width"] = value
        elif key in ("align-items", "align-self", "justify-items", "justify-self",
                     "align-content", "justify-content"):
            result[key.replace("-", "_")] = alignment(value)
        elif key in ("flex-grow", "flex-shrink", "scrollbar-width", "aspect-ratio"):
            result[key.replace("-", "_")] = float(value)
        elif key == "flex-line-count":
            result["flex_line_count"] = int(value)
        elif key in ("flex-basis", "grid-template-columns", "grid-template-rows",
                     "grid-auto-rows", "grid-auto-columns"):
            result[key.replace("-", "_")] = value
        elif key.startswith(("grid-row-", "grid-column-")):
            _, axis, endpoint = key.split("-")
            if endpoint not in ("start", "end"):
                raise ValueError(f"Unsupported Grid endpoint {key!r}")
            result.setdefault(f"grid_{axis}", {})[endpoint] = value
        elif key == "writing-mode" and element.tag == "text":
            if value != "vertical-lr":
                raise ValueError(f"Unsupported text measurement writing mode {value!r}")
        else:
            raise ValueError(f"Unknown upstream attribute {key!r}")
    return result


def read_case(path, root, index):
    raw = path.read_bytes()
    document = ET.fromstring(raw)
    relative = path.relative_to(root).as_posix()
    if document.tag != "test" or document.get("name") != path.stem:
        raise ValueError(f"Invalid fixture identity: {relative}")
    viewport = document.find("viewport")
    input_nodes = document.find("input")
    expected_nodes = document.find("expectations")
    if viewport is None or input_nodes is None or expected_nodes is None:
        raise ValueError(f"Incomplete fixture: {relative}")
    if len(input_nodes) != 1 or len(expected_nodes) != 1:
        raise ValueError(f"Expected a single root: {relative}")
    nodes, expected = [], []

    def visit(element, expectation, parent):
        if element.tag not in ("div", "text") or expectation.tag != "node":
            raise ValueError(f"Unknown fixture node: {relative}")
        number = len(nodes)
        node = {"id": number + 1, "parent": parent, "style": style(element)}
        if element.tag == "text":
            if len(element):
                raise ValueError(f"Measured node has children: {relative}")
            if element.text is not None:
                node["measure"] = {"ahem_text": element.text.strip(),
                                   "vertical": element.get("writing-mode") == "vertical-lr"}
        nodes.append(node)
        if len(element) != len(expectation):
            raise ValueError(f"Input/expectation topology differs: {relative}")
        validate_expectation(expectation.attrib)
        expected.append(dict(expectation.attrib))
        for child, child_expected in zip(element, expectation, strict=True):
            visit(child, child_expected, number)

    visit(input_nodes[0], expected_nodes[0], None)
    spaces = []
    for axis in ("width", "height"):
        value = viewport.get(axis, "max-content")
        if value in ("min-content", "max-content"):
            spaces.append(value)
        else:
            numeric = float(value.removesuffix("px"))
            if not math.isfinite(numeric) or not 0 <= numeric <= 1_000_000:
                raise ValueError(f"Invalid viewport: {relative}")
            spaces.append(numeric)
    rounding = document.get("use-rounding", "true")
    if rounding not in ("true", "false"):
        raise ValueError(f"Invalid rounding flag: {relative}")
    return {"name": f"upstream-{index:04d}-{relative.split('/')[0]}",
            "available": spaces, "nodes": nodes,
            "source": {"path": relative, "sha256": hashlib.sha256(raw).hexdigest()},
            "use_rounding": rounding == "true", "xml_expected": expected}


def corpus(source_dir):
    root = Path(source_dir) / "tests" / "xml"
    paths = sorted(root.rglob("*.xml"), key=lambda path: path.relative_to(root).as_posix())
    if len(paths) != 6020:
        raise ValueError(f"Pinned corpus should contain 6020 XML fixtures, found {len(paths)}")
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.relative_to(root).as_posix().encode("utf-8") + b"\x00")
        digest.update(path.read_bytes() + b"\x00")
    if digest.hexdigest() != CORPUS_SHA256:
        raise ValueError("Upstream XML corpus differs from the pinned source archive")
    cases = [read_case(path, root, index) for index, path in enumerate(paths)]
    groups = collections.Counter(case["source"]["path"].split("/")[0] for case in cases)
    return {"protocol": "AXISLY_UPSTREAM_CORPUS_V1", "taffy": "0.14.0",
            "commit": TAFFY_COMMIT, "archive_sha256": ARCHIVE_SHA256,
            "corpus_sha256": CORPUS_SHA256,
            "groups": dict(sorted(groups.items())), "cases": cases}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    result = corpus(args.source)
    Path(args.output).write_text(json.dumps(result, ensure_ascii=True,
                                           separators=(",", ":")), encoding="utf-8")
    print(json.dumps({"cases": len(result["cases"]), "groups": result["groups"]}))


if __name__ == "__main__":
    main()
