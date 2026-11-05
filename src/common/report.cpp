#include "sigscan/report.hpp"

#ifdef _WIN32
#include <cstdlib>
#else
#include <unistd.h>
#endif

#include <cinttypes>

namespace sigscan {

std::string csv_header() {
    return "impl,variant,layout,mode,dataset,threads,procs,chunk,block,text_bytes,npat,minlen,maxlen,"
           "automaton_bytes,overlap_bytes,rep,io_ms,build_ms,h2d_ms,search_ms,d2h_ms,merge_ms,comm_ms,total_ms,"
           "matches,checksum,host,extra";
}

std::string to_csv(const Record& r) {
    char buf[1024];
    std::snprintf(buf, sizeof buf,
                  "%s,%s,%s,%s,%s,%d,%d,%" PRIu64 ",%d,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                  ",%" PRIu64 ",%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%" PRIu64 ",%016" PRIx64 ",%s,%s",
                  r.impl.c_str(), r.variant.c_str(), r.layout.c_str(), r.mode.c_str(), r.dataset.c_str(), r.threads,
                  r.procs, r.chunk, r.block, r.text_bytes, r.npat, r.minlen, r.maxlen, r.automaton_bytes,
                  r.overlap_bytes, r.rep, r.io_ms, r.build_ms, r.h2d_ms, r.search_ms, r.d2h_ms, r.merge_ms,
                  r.comm_ms, r.total_ms, r.result.count, r.result.checksum, hostname().c_str(),
                  r.extra.c_str());
    return buf;
}

std::string hostname() {
    static const std::string name = [] {
#ifdef _WIN32
        const char* env = std::getenv("COMPUTERNAME");
        std::string s(env ? env : "unknown");
#else
        char h[256] = {};
        if (gethostname(h, sizeof h - 1) != 0) return std::string("unknown");
        std::string s(h);
#endif
        if (auto dot = s.find('.'); dot != std::string::npos) s.erase(dot);
        return s;
    }();
    return name;
}

void add_common_options(Cli& cli, const std::string& default_layout) {
    cli.opt("text", "", "input byte stream (raw file)")
        .opt("patterns", "", "patterns file, one hex string per line (*.pat)")
        .opt("mode", "count", "count = number + checksum of matches; list = full (pattern_id, offset) list")
        .opt("layout", default_layout, "automaton layout: dense | compressed")
        .opt("out", "", "write matches (list mode) as CSV to this file")
        .opt("reps", "1", "measured repetitions")
        .opt("warmup", "0", "unmeasured warm-up runs before the measured ones")
        .flag("csv", "print one CSV record per repetition on stdout")
        .flag("csv-header", "print the CSV header line first")
        .flag("quiet", "no human-readable summary on stderr");
}

Inputs load_inputs(const Cli& cli) {
    if (cli.str("text").empty() || cli.str("patterns").empty())
        throw std::runtime_error("--text and --patterns are required (see --help)");
    if (cli.str("mode") != "count" && cli.str("mode") != "list")
        throw std::runtime_error("--mode must be count or list");
    Inputs in;
    Timer t;
    in.text = read_file(cli.str("text"));
    in.pats = load_patterns(cli.str("patterns"));
    in.io_ms = t.ms();
    std::string stem = cli.str("text");
    if (auto slash = stem.find_last_of("/\\"); slash != std::string::npos) stem.erase(0, slash + 1);
    if (auto dot = stem.find_last_of('.'); dot != std::string::npos) stem.erase(dot);
    in.dataset = stem;
    return in;
}

Record base_record(const Cli& cli, const Inputs& in, const std::string& impl) {
    Record r;
    r.impl = impl;
    r.layout = cli.str("layout");
    r.mode = cli.str("mode");
    r.dataset = in.dataset;
    r.text_bytes = in.text.size();
    r.npat = in.pats.size();
    r.minlen = in.pats.min_len;
    r.maxlen = in.pats.max_len;
    r.io_ms = in.io_ms;
    return r;
}

void emit(const Cli& cli, const Record& r) {
    if (cli.on("csv")) std::printf("%s\n", to_csv(r).c_str());
    if (!cli.on("quiet")) {
        const double mbps = r.search_ms > 0 ? double(r.text_bytes) / 1e6 / (r.search_ms / 1e3) : 0.0;
        std::fprintf(stderr,
                     "[%s %s %s] rep %d: build %.2f ms, search %.2f ms (%.1f MB/s), total %.2f ms, "
                     "matches %" PRIu64 ", checksum %016" PRIx64 "\n",
                     r.impl.c_str(), r.variant.c_str(), r.layout.c_str(), r.rep, r.build_ms, r.search_ms, mbps,
                     r.total_ms, r.result.count, r.result.checksum);
    }
}

}  // namespace sigscan
