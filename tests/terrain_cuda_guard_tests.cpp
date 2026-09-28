// Exercise the CUDA rejections on CPU CI, without requiring nvcc or a GPU.
#include "ember/terrain.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
bool rejects_with(ember::SimulationConfig config, const char* label) {
    try { ember::resolve_terrain_config(config); }
    catch (const std::invalid_argument& error) {
        if (std::string(error.what()).find("EMBER_ENABLE_CUDA=OFF") != std::string::npos) {
            std::cout << "[PASS] " << label << '\n';
            return true;
        }
    }
    std::cout << "[FAIL] " << label << '\n';
    return false;
}
} // namespace

int main() {
    ember::SimulationConfig terrain_config;
    terrain_config.terrain_path = "does-not-exist.asc";
    const bool terrain_ok = rejects_with(terrain_config, "CUDA rejects real terrain before loading");

    ember::SimulationConfig rothermel_config;
    rothermel_config.spread_model = ember::SpreadModel::Rothermel;
    const bool rothermel_ok = rejects_with(rothermel_config, "CUDA rejects the Rothermel spread model");

    return (terrain_ok && rothermel_ok) ? 0 : 1;
}
