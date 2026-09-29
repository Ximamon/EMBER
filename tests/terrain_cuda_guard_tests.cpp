/**
 * @file terrain_cuda_guard_tests.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Test verifying rejection of real terrain on CUDA-enabled builds prior to parity.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

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
