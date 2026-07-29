#pragma once

#include "ember/config.hpp"

#include <iosfwd>

namespace ember {

struct CliOptions {
    SimulationConfig config;
    bool show_help{};
};

CliOptions parse_cli(int argc, const char* const argv[]);
void print_help(std::ostream& output);

} // namespace ember
