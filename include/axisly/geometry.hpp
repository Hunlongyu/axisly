#pragma once

namespace axisly {

using Unit = float;

struct Size {
    Unit width{};
    Unit height{};

    bool operator==(const Size&) const = default;
};

struct Rect {
    Unit x{};
    Unit y{};
    Unit width{};
    Unit height{};

    bool operator==(const Rect&) const = default;
};

struct Insets {
    Unit left{};
    Unit top{};
    Unit right{};
    Unit bottom{};

    bool operator==(const Insets&) const = default;
};

} // namespace axisly
