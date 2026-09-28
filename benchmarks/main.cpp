#include "cases.hpp"
#include "allocation_probe.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <string_view>

namespace {
using namespace axisly::bench;
struct Options {
    std::size_t iterations{100}, samples{11}, warmup{3};
    std::string name, output;
    bool list{}, help{}, self_test{};
};
std::size_t positive(std::string_view text) {
    std::size_t value{};
    const auto parsed = std::from_chars(text.data(),text.data()+text.size(),value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data()+text.size() || value == 0 || value > 1000000)
        throw std::invalid_argument("Numeric arguments must be integers in [1, 1000000]");
    return value;
}
Options parse(int argc,char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--smoke") { o.iterations = 2; o.samples = 3; o.warmup = 1; }
        else if (arg == "--list") o.list = true;
        else if (arg == "--help") o.help = true;
        else if (arg == "--self-test") o.self_test = true;
        else if (arg == "--iterations" || arg == "--samples" || arg == "--warmup" || arg == "--case" || arg == "--output") {
            if (++i >= argc) throw std::invalid_argument("Missing argument value");
            if (arg == "--iterations") o.iterations = positive(argv[i]);
            if (arg == "--samples") o.samples = positive(argv[i]);
            if (arg == "--warmup") o.warmup = positive(argv[i]);
            if (arg == "--case") o.name = argv[i];
            if (arg == "--output") o.output = argv[i];
        } else throw std::invalid_argument("Unknown benchmark argument: "+std::string(arg));
    }
    return o;
}
std::string compiler() {
#if defined(__clang__)
    return "Clang "+std::to_string(__clang_major__)+"."+std::to_string(__clang_minor__);
#elif defined(_MSC_FULL_VER)
    return "MSVC "+std::to_string(_MSC_FULL_VER);
#elif defined(__GNUC__)
    return "GCC "+std::to_string(__GNUC__)+"."+std::to_string(__GNUC_MINOR__);
#else
    return "unknown";
#endif
}
struct Sample { double us{}, checksum{}, measurements{}, layouts{}; AllocationStats memory; };
Sample sample(const Case& c,std::size_t iterations) {
    Sample s;
#ifdef AXISLY_COUNT_ALLOCATIONS
    begin_allocations();
#endif
    const auto start = std::chrono::steady_clock::now();
    try {
        for (std::size_t i = 0; i < iterations; ++i) {
            const auto observed = run(c,i);
            s.checksum += observed.checksum;
            s.measurements += static_cast<double>(observed.measurements);
            s.layouts += static_cast<double>(observed.layouts);
        }
    } catch (...) {
#ifdef AXISLY_COUNT_ALLOCATIONS
        (void)end_allocations();
#endif
        throw;
    }
    const auto end = std::chrono::steady_clock::now();
#ifdef AXISLY_COUNT_ALLOCATIONS
    s.memory = end_allocations();
    if (s.memory.live_allocations != 0 || s.memory.live_bytes != 0)
        throw std::runtime_error("Benchmark allocations escaped the measured call lifetime");
#endif
    const auto count = static_cast<double>(iterations);
    s.us = std::chrono::duration<double,std::micro>(end-start).count()/count;
    s.measurements /= count;
    s.layouts /= count;
    return s;
}
void report(std::ostream& out,const Case& c,const Options& options) {
    validate(c);
    for (std::size_t i = 0; i < options.warmup; ++i) (void)run(c,i);
    std::vector<Sample> samples;
    samples.reserve(options.samples);
    for (std::size_t i = 0; i < options.samples; ++i) samples.push_back(sample(c,options.iterations));
    const auto& first = samples.front();
    for (const auto& s : samples)
        if (s.checksum != first.checksum || s.measurements != first.measurements || s.layouts != first.layouts ||
            s.memory.allocations != first.memory.allocations || s.memory.requested_bytes != first.memory.requested_bytes ||
            s.memory.peak_live_bytes != first.memory.peak_live_bytes)
            throw std::runtime_error("Benchmark samples are not repeatable: "+c.name);
    out << c.name << ',' << (c.nested ? "host_adapter" : "core") << ',' << AXISLY_BENCH_VERSION << ',' << AXISLY_BENCH_CONFIG << ',' << compiler()
        << ',' << (c.nested ? 5 : c.items.size()) << ',' << (c.nested ? 2 : c.spec.columns.size())
        << ',' << (c.nested ? 1 : c.spec.rows.size()) << ',' << c.containers.size() << ',' << options.iterations
        << ',' << options.samples << ',' << options.warmup << ',';
#ifdef AXISLY_COUNT_ALLOCATIONS
    out << "allocations,,," << static_cast<double>(first.memory.allocations)/static_cast<double>(options.iterations) << ','
        << static_cast<double>(first.memory.requested_bytes)/static_cast<double>(options.iterations) << ',' << first.memory.peak_live_bytes;
#else
    std::vector<double> times;
    times.reserve(samples.size());
    for (const auto& s : samples) times.push_back(s.us);
    std::sort(times.begin(),times.end());
    const auto middle = times.size()/2;
    const auto median = times.size()%2 == 0 ? (times[middle-1]+times[middle])/2 : times[middle];
    const auto p95 = static_cast<std::size_t>(std::ceil(static_cast<double>(times.size())*0.95))-1;
    out << "timing," << median << ',' << times[p95] << ",,,";
#endif
    out << ',' << first.measurements << ',' << first.layouts << ',' << first.checksum << '\n';
}
}

int main(int argc,char** argv) {
    try {
        const auto options = parse(argc,argv);
        if (options.help) {
            std::cout << "axisly benchmark [--case NAME] [--iterations N] [--samples N] [--warmup N] [--output CSV]\n"
                         "                 [--list] [--smoke] [--self-test (allocation executable only)]\n";
            return 0;
        }
        if (options.self_test) {
#ifdef AXISLY_COUNT_ALLOCATIONS
            allocation_self_test();
            std::cout << "PASS allocation probe contracts\n";
            return 0;
#else
            throw std::invalid_argument("Use axisly_bench_allocations --self-test");
#endif
        }
        const auto cases = make_cases();
        if (options.list) { for (const auto& c : cases) std::cout << c.name << '\n'; return 0; }
        if (!options.name.empty() && std::none_of(cases.begin(),cases.end(),[&](const Case& c){ return c.name == options.name; }))
            throw std::invalid_argument("Unknown benchmark case: "+options.name);
        std::ofstream file;
        if (!options.output.empty()) {
            file.open(options.output,std::ios::trunc);
            if (!file) throw std::runtime_error("Cannot open output: "+options.output);
        }
        auto& out = options.output.empty() ? std::cout : file;
        out.imbue(std::locale::classic());
        out << std::fixed << std::setprecision(6);
        out << "case,scope,version,build,compiler,items,columns,rows,containers,iterations,samples,warmup,mode,median_us,p95_batch_us,allocations_per_layout,requested_bytes_per_layout,peak_live_bytes,measurements_per_layout,core_layouts_per_run,checksum\n";
        for (const auto& c : cases) if (options.name.empty() || options.name == c.name) report(out,c,options);
        out.flush();
        if (!out) throw std::runtime_error("Failed to write benchmark report");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
