// Compile the loader with unsupported backend flags without their runtime dependencies.
#include "ember/terrain.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    ember::SimulationConfig config;
    config.terrain_path = "does-not-exist.asc";
    config.elevation_path = "does-not-exist-elevation.asc";
    try { ember::resolve_terrain_config(config); }
    catch (const std::invalid_argument& error) {
        if (std::string(error.what()).find("elevation requires scalar CPU") != std::string::npos) {
            std::cout << "[PASS] unsupported backend rejects elevation before loading\n";
            return 0;
        }
    }
    return 1;
}
