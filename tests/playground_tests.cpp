#include "scenes.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace axisly;
using namespace axisly::playground;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a-b) < 0.01f; }
void geometry(Rect r) {
    check(std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height),"Nonfinite rectangle");
    check(r.x >= 0 && r.y >= 0 && r.width >= 0 && r.height >= 0,"Negative rectangle");
}
}

int main() {
    try {
        std::size_t runs = 0;
        for (std::size_t scene = 0; scene < scenes.size(); ++scene) {
            for (const auto size : {Size{0,0}, Size{120,80}, Size{480,320}, Size{1012,410}, Size{1800,1000}}) {
                const auto result = arrange_scene(scene,size);
                check(result.succeeded(),"Scene failed to compute");
                check(!result.items.empty(),"Empty scene");
                for (const auto& item : result.items) { geometry(item.rect); geometry(item.cell); }
                const auto repeat = arrange_scene(scene,size);
                check(repeat.occupied == result.occupied && repeat.items.size() == result.items.size(),"Nondeterministic extent");
                for (std::size_t i = 0; i < result.items.size(); ++i)
                    check(repeat.items[i].rect == result.items[i].rect,"Nondeterministic item");
                check(std::all_of(result.columns.begin(),result.columns.end(),[](float v){return v >= 0 && std::isfinite(v);}),"Invalid column diagnostic");
                ++runs;
            }
        }
        const auto fixed = arrange_scene(0,{1000,500});
        check(fixed.columns == std::vector<Unit>{140,220,100},"Fixed column diagnostics wrong");
        check(fixed.rows == std::vector<Unit>{80,120},"Fixed row diagnostics wrong");
        const auto weights = arrange_scene(2,{1000,500});
        check(near(weights.columns[1],weights.columns[0]*2) && near(weights.columns[2],weights.columns[0]*3),"Weight ratio wrong");
        const auto col_span = arrange_scene(5,{1000,500});
        check(near(col_span.items.front().cell.width,968),"Column span excludes gaps");
        const auto row_span = arrange_scene(6,{1000,500});
        check(near(row_span.items.front().cell.height,468),"Row span excludes gaps");
        const auto align = arrange_scene(7,{1000,500});
        check(align.items.size() == 16,"Missing alignment combinations");
        for (const auto& item : align.items) {
            check(item.rect.x >= item.cell.x && item.rect.y >= item.cell.y &&
                item.rect.x+item.rect.width <= item.cell.x+item.cell.width+0.01f &&
                item.rect.y+item.rect.height <= item.cell.y+item.cell.height+0.01f,"Alignment outside cell");
        }
        const auto small = arrange_scene(4,{240,300});
        const auto large = arrange_scene(4,{1000,300});
        check(small.occupied.height > large.occupied.height,"Content did not wrap");
        const auto range = arrange_scene(8,{1600,500});
        check(near(range.columns[0],220),"Range maximum ignored");
        check(arrange_scene(8,{120,500}).occupied.width > 120,"Range minimum did not overflow");
        const auto nested = arrange_scene(9,{1000,500});
        check(nested.items.size() == 5 && nested.items[2].depth == 1
            && nested.items[2].input.node != nested.items[1].input.node,
            "Nested nodes must have distinct identities in the same CSS tree");
        check(nested.items[2].rect.x > nested.items[1].rect.x && nested.items[2].rect.y > nested.items[1].rect.y,"Child origin not translated");
        bool invalid = false;
        try { (void)arrange_scene(10,{1,1}); } catch (const std::out_of_range&) { invalid = true; }
        check(invalid,"Unknown scene accepted");
        std::cout << "PASS: " << runs << " scene/size contracts and semantic assertions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
