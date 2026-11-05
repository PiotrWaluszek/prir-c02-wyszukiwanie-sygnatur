// Small command-line parser with --help.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "sigscan/common.hpp"

namespace sigscan {

// "512M" -> 536870912 (k/M/G are binary)
u64 parse_size(const std::string& s);

class Cli {
public:
    Cli(std::string prog, std::string about);

    Cli& opt(const std::string& name, const std::string& def, const std::string& help);
    Cli& flag(const std::string& name, const std::string& help);

    // Exits on --help, throws on unknown options.
    void parse(int argc, char** argv);

    const std::string& str(const std::string& name) const;
    long long integer(const std::string& name) const;
    double real(const std::string& name) const;
    u64 size(const std::string& name) const;
    bool on(const std::string& name) const;
    bool given(const std::string& name) const;
    std::string help() const;

private:
    struct Opt {
        std::string value, help;
        bool is_flag = false, given = false;
    };
    const Opt& get(const std::string& name) const;
    std::string prog_, about_;
    std::map<std::string, Opt> opts_;
    std::vector<std::string> order_;
};

}  // namespace sigscan
