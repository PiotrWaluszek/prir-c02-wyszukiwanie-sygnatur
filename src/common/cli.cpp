#include "sigscan/cli.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace sigscan {

u64 parse_size(const std::string& s) {
    if (s.empty()) throw std::runtime_error("empty size");
    std::size_t idx = 0;
    const double v = std::stod(s, &idx);
    u64 mult = 1;
    if (idx < s.size()) {
        switch (s[idx]) {
            case 'k': case 'K': mult = 1ULL << 10; break;
            case 'm': case 'M': mult = 1ULL << 20; break;
            case 'g': case 'G': mult = 1ULL << 30; break;
            default: throw std::runtime_error("bad size suffix in '" + s + "'");
        }
        ++idx;
        if (idx < s.size() && (s[idx] == 'i' || s[idx] == 'I')) ++idx;
        if (idx < s.size() && (s[idx] == 'b' || s[idx] == 'B')) ++idx;
        if (idx != s.size()) throw std::runtime_error("bad size '" + s + "'");
    }
    if (v < 0) throw std::runtime_error("negative size '" + s + "'");
    return u64(v * double(mult) + 0.5);
}

Cli::Cli(std::string prog, std::string about) : prog_(std::move(prog)), about_(std::move(about)) {}

Cli& Cli::opt(const std::string& name, const std::string& def, const std::string& help) {
    opts_[name] = Opt{def, help, false, false};
    order_.push_back(name);
    return *this;
}

Cli& Cli::flag(const std::string& name, const std::string& help) {
    opts_[name] = Opt{"0", help, true, false};
    order_.push_back(name);
    return *this;
}

void Cli::parse(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            std::fputs(help().c_str(), stdout);
            std::exit(0);
        }
        if (a.rfind("--", 0) != 0) throw std::runtime_error("unexpected argument '" + a + "' (see --help)");
        a.erase(0, 2);
        std::string val;
        bool has_val = false;
        if (auto eq = a.find('='); eq != std::string::npos) {
            val = a.substr(eq + 1);
            a.erase(eq);
            has_val = true;
        }
        auto it = opts_.find(a);
        if (it == opts_.end()) throw std::runtime_error("unknown option --" + a + " (see --help)");
        Opt& o = it->second;
        o.given = true;
        if (o.is_flag) {
            o.value = has_val ? val : "1";
        } else if (has_val) {
            o.value = val;
        } else {
            if (i + 1 >= argc) throw std::runtime_error("option --" + a + " needs a value");
            o.value = argv[++i];
        }
    }
}

const Cli::Opt& Cli::get(const std::string& name) const {
    auto it = opts_.find(name);
    if (it == opts_.end()) throw std::logic_error("option not declared: " + name);
    return it->second;
}

const std::string& Cli::str(const std::string& name) const { return get(name).value; }
long long Cli::integer(const std::string& name) const { return std::stoll(get(name).value); }
double Cli::real(const std::string& name) const { return std::stod(get(name).value); }
u64 Cli::size(const std::string& name) const { return parse_size(get(name).value); }
bool Cli::on(const std::string& name) const { return get(name).value != "0"; }
bool Cli::given(const std::string& name) const { return get(name).given; }

std::string Cli::help() const {
    std::string h = prog_ + " - " + about_ + "\n\nOptions:\n";
    for (const auto& name : order_) {
        const Opt& o = opts_.at(name);
        std::string left = "  --" + name + (o.is_flag ? "" : " <v>");
        if (left.size() < 26) left.resize(26, ' ');
        else left += "  ";
        h += left + o.help;
        if (!o.is_flag && !o.value.empty()) h += " [default: " + o.value + "]";
        h += "\n";
    }
    h += "  --help                  show this help\n";
    return h;
}

}  // namespace sigscan
