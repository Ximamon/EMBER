// Exercise the CUDA rejection on CPU CI, without requiring nvcc or a GPU.
#include "ember/terrain.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
int main() {
    ember::SimulationConfig config;
    config.terrain_path = "does-not-exist.asc";
    try { ember::resolve_terrain_config(config); }
    catch (const std::invalid_argument& error) {
        if (std::string(error.what()).find("EMBER_ENABLE_CUDA=OFF") != std::string::npos) {
            std::cout << "[PASS] CUDA rejects real terrain before loading\n";
            return 0;
        }
    }
    return 1;
}
