#pragma once

#include "css_reference_output.hpp"

#include <array>
#include <initializer_list>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct UpstreamFixture {
    const char* name;
    void (*write)(std::ostream&);
};

[[nodiscard]] std::vector<UpstreamFixture> upstream_fixtures();
[[nodiscard]] axisly::css::MeasureFunction ahem_measurement(std::string text,
    bool vertical);

// Large explicit lists remain explicit lists at runtime. Repeated identical
// entries are emitted as typed runs to keep compiler work proportional to the
// source information instead of thousands of identical constructor expressions.
template<class Track>
std::vector<Track> fixture_track_runs(
    std::initializer_list<std::pair<std::size_t, Track>> runs) {
    std::size_t count = 0;
    for (const auto& run : runs) count += run.first;
    std::vector<Track> tracks;
    tracks.reserve(count);
    for (const auto& run : runs) tracks.insert(tracks.end(), run.first, run.second);
    return tracks;
}
