"""Compare resolved Grid tracks and occupied line ranges in the tree protocol."""

import math


def compare_grid_details(native, oracle):
    for actual_node, expected_node in zip(native["nodes"], oracle["nodes"], strict=True):
        actual = actual_node["grid"]
        expected = expected_node["grid"]
        assert (actual is None) == (expected is None), (native["name"], actual_node["id"])
        if expected is None:
            continue
        for axis in ("rows", "columns"):
            for field in ("negative", "explicit", "positive"):
                assert actual[axis][field] == expected[axis][field], (
                    native["name"], actual_node["id"], axis, field,
                    actual[axis][field], expected[axis][field])
            observed = actual[axis]["positions"]
            target = expected[axis]["positions"]
            assert len(observed) == len(target), (
                native["name"], actual_node["id"], axis, "track count",
                len(observed), len(target))
            for index, (got, want) in enumerate(zip(observed, target, strict=True)):
                assert all(math.isclose(x, y, rel_tol=0, abs_tol=0.01)
                           for x, y in zip(got, want, strict=True)), (
                    native["name"], actual_node["id"], axis, index, got, want)
        assert sorted(actual["items"]) == sorted(expected["items"]), (
            native["name"], actual_node["id"], "items",
            actual["items"], expected["items"])
