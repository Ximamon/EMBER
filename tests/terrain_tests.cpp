/**
 * @file terrain_tests.cpp
 * @author Juaquín Berná (@Ximamon)
 * @brief Integration tests for ESRI ASCII Grid terrain loading, projection, and geometry.
 * @version 0.5
 * @date 29/7/2026
 * 
 * 
 */

#include "ember/terrain.hpp"
#include "ember/simulation.hpp"
#include "ember/runner.hpp"
#include "ember/cli.hpp"
#include "ember/rothermel.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("check failed: " #x); } while (false)
template<class F> void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}

int main() {
    try {
        const std::filesystem::path source = EMBER_FIXTURES_DIR;
        const auto fixture = source / "terrain.asc";
        const auto terrain = ember::load_terrain(fixture);
        CHECK(terrain->width == 4 && terrain->height == 3);
        CHECK(terrain->xllcorner == 420000 && terrain->yllcorner == 4590000);
        CHECK(terrain->cell_size_m == 10 && terrain->codes.front() == 0 && terrain->codes.back() == 165);
        CHECK(terrain->valid_cells == 10 && terrain->combustible_cells == 6);
        // National metric grids and Canary Islands use the same raster contract.
        const auto projection_dir = std::filesystem::temp_directory_path() / "ember-national-projection-tests";
        std::filesystem::create_directories(projection_dir);
        std::filesystem::copy_file(fixture, projection_dir / "terrain.asc", std::filesystem::copy_options::overwrite_existing);
        for (int epsg : {32628, 3035}) {
            { std::ofstream out(projection_dir / "terrain.prj"); out << "ID[\"EPSG\"," << epsg << ']'; }
            CHECK(ember::load_terrain(projection_dir / "terrain.asc")->epsg == epsg);
        }
        std::filesystem::remove_all(projection_dir);
        CHECK(!terrain->valid[0] && terrain->valid[1]);
        ember::SimulationConfig config;
        config.terrain = terrain;
        config.max_steps = 8;
        config.base_spread = 1;
        auto resolved = ember::resolve_terrain_config(config);
        CHECK(resolved.terrain == terrain);
        CHECK(resolved.width == 4 && resolved.height == 3);
        CHECK(resolved.ignitions[0].x == 1 && resolved.ignitions[0].y == 1);
        ember::WildfireSimulation first(config, 0), second(config, 1);
        first.initialize(); second.initialize();
        auto a = first.grid().current_view();
        auto b = second.grid().current_view();
        auto next = first.grid().next_view();
        for (std::size_t i = 0; i < 12; ++i) {
            CHECK(a.state[i] == b.state[i] && a.fuel[i] == b.fuel[i]);
            CHECK(a.state[i] == next.state[i] && a.fuel[i] == next.fuel[i]);
            CHECK(a.moisture[i] == 0.2F && a.vegetation[i] == 1 && a.elevation[i] == 0);
        }
        a.fuel[3] = 0.123F;
        CHECK(b.fuel[3] == 1); // independent scenario buffers
        const auto stats = first.run(); // must reset from real terrain, not synthetic noise
        CHECK(stats.valid_cells == 10 && stats.nodata_cells == 2 && stats.non_combustible_cells == 4);
        CHECK(stats.initially_combustible_cells == 6);
        CHECK(std::abs(stats.burned_hectares - static_cast<double>(stats.burned_cells) * .01) < 1e-9);
        CHECK(std::abs(stats.combustible_burned_percent - static_cast<double>(stats.burned_cells) / 6 * 100) < 1e-9);
        ember::WildfireSimulation repeat(config, 0);
        repeat.run();
        a = first.grid().current_view(); b = repeat.grid().current_view();
        for (std::size_t i = 0; i < 12; ++i) {
            CHECK(a.state[i] == b.state[i] && a.fuel[i] == b.fuel[i]);
            if (!ember::combustible_code(terrain->codes[i])) {
                CHECK(a.state[i] == ember::CellState::NonCombustible && a.fuel[i] == 0);
            }
        }
        for (const auto point : {ember::IgnitionPoint{0, 0}, {1, 0}, {2, 0}, {4, 0}}) {
            auto invalid = config; invalid.ignitions = {point};
            rejects([&] { ember::WildfireSimulation sim(invalid, 0); });
        }
        auto invalid = config; invalid.width_explicit = true; invalid.width = 5;
        rejects([&] { ember::resolve_terrain_config(invalid); });
        const auto path = fixture.string();
        const char* args[] = {"ember", "--terrain", path.c_str(), "--width", "4", "--height", "3",
                              "--terrain-fuel", "0.7", "--terrain-moisture", "0.4"};
        auto cli = ember::resolve_terrain_config(ember::parse_cli(11, args).config);
        CHECK(cli.width == 4 && cli.height == 3 && cli.terrain_fuel == .7F && cli.terrain_moisture == .4F);
        // Ignition bounds must be checked after deriving dimensions, including maps wider than 512.
        const char* deferred[] = {"ember", "--terrain", path.c_str(), "--ignition", "600,0"};
        const auto parsed = ember::parse_cli(5, deferred);
        rejects([&] { ember::resolve_terrain_config(parsed.config); });
        const auto directory = std::filesystem::current_path() / "terrain-test-artifacts";
        std::filesystem::create_directories(directory);
        const auto elevation_path = directory / "elevation.asc";
        std::filesystem::copy_file(source / "terrain.prj", directory / "elevation.prj",
                                   std::filesystem::copy_options::overwrite_existing);
        const std::string elevation_header = "ncols 4\nnrows 3\nxllcorner 420000\nyllcorner 4590000\ncellsize 10\nNODATA_value -9999\n";
        const auto write_heights = [&](const std::string& header, const std::string& values) {
            std::ofstream out(elevation_path); out << header << values;
        };
        write_heights(elevation_header, "-9999 -5 0 10 20 30 40 50 60 70 80 90");
        auto relief_config = config;
        relief_config.elevation_path = elevation_path.string();
#if AVX2 || EMBER_ENABLE_MPI
        rejects([&] { ember::resolve_terrain_config(relief_config); });
#else
        auto relief = ember::resolve_terrain_config(relief_config);
        CHECK((*relief.elevation)[0] == 0 && (*relief.elevation)[1] == -5 && (*relief.elevation)[11] == 90);
        ember::WildfireSimulation relief_sim(relief, 0);
        relief_sim.initialize();
        CHECK(relief_sim.grid().current_view().elevation[5] == 30);
        CHECK(ember::resolve_terrain_config(relief).elevation == relief.elevation);
        relief.wind_strength = 0;
        relief.base_spread = .5F;
        const auto probability = [&](float target, int dx, int dy) {
            return ember::WildfireSimulation::neighbor_ignition_probability(
                relief, 1, 0, 1, target, 0, dx, dy);
        };
        CHECK(std::abs(probability(5, 1, 0) - .625F) < 1e-6F);
        CHECK(std::abs(probability(-5, 1, 0) - .375F) < 1e-6F);
        const float diagonal = 1 / std::sqrt(2.0F);
        CHECK(std::abs(probability(5, 1, 1) - .5F * (1 + .25F * diagonal) * diagonal) < 1e-6F);
        // Check the actual Rothermel kernel with metric relief and diagonal geometry.
        auto physical = relief;
        physical.spread_model = ember::SpreadModel::Rothermel;
        physical.terrain_moisture = .05F;
        physical.wind_speed_m_s = 0;
        physical.rothermel_time_step_s = 1;
        physical.min_spread_rate_m_s = 0;
        ember::WildfireSimulation physical_sim(physical, 0);
        physical_sim.initialize();
        auto input = physical_sim.grid().current_view();
        for (std::size_t i = 0; i < 12; ++i) {
            input.state[i] = ember::CellState::NonCombustible;
            input.elevation[i] = 0;
            input.fuel_class[i] = static_cast<std::uint8_t>(ember::FuelClass::Shrub);
        }
        input.state[5] = ember::CellState::Burning;
        for (auto target : {6, 9, 10}) input.state[target] = ember::CellState::Unburned;
        input.elevation[6] = 1; input.elevation[9] = -1; input.elevation[10] = 1;
        physical_sim.step(0);
        const auto reached = physical_sim.grid().current_view();
        const auto params = ember::fuel_model_params(ember::FuelClass::Shrub);
        const float uphill = ember::rothermel_spread_rate_m_s(params, .05F, 0, .1F) / 10;
        const float downhill = ember::rothermel_spread_rate_m_s(params, .05F, 0, -.1F) / 10;
        const float diagonal_front = ember::rothermel_spread_rate_m_s(params, .05F, 0, .1F * diagonal) * diagonal / 10;
        CHECK(std::abs(reached.burn_fraction[6] - uphill) < 1e-7F);
        CHECK(std::abs(reached.burn_fraction[9] - downhill) < 1e-7F);
        CHECK(std::abs(reached.burn_fraction[10] - diagonal_front) < 1e-7F);
        CHECK(uphill > downhill);
        auto no_terrain = ember::SimulationConfig{};
        no_terrain.elevation_path = elevation_path.string();
        rejects([&] { ember::resolve_terrain_config(no_terrain); });
#endif
        for (const auto& values : {"0 -9999 0 10 20 30 40 50 60 70 80 90",
                                  "0 nan 0 10 20 30 40 50 60 70 80 90",
                                  "0 inf 0 10 20 30 40 50 60 70 80 90",
                                  "0 0 0", "0 0 0 0 0 0 0 0 0 0 0 0 0"}) {
            write_heights(elevation_header, values);
            rejects([&] { ember::load_elevation(elevation_path, *terrain); });
        }
        for (const auto& replacement : {std::make_pair("ncols 4", "ncols 3"),
                                        std::make_pair("nrows 3", "nrows 4"),
                                        std::make_pair("xllcorner 420000", "xllcorner 420001"),
                                        std::make_pair("yllcorner 4590000", "yllcorner 4590001"),
                                        std::make_pair("cellsize 10", "cellsize 20")}) {
            auto shifted = elevation_header;
            shifted.replace(shifted.find(replacement.first), std::string(replacement.first).size(), replacement.second);
            write_heights(shifted, "0 0 0 0 0 0 0 0 0 0 0 0");
            rejects([&] { ember::load_elevation(elevation_path, *terrain); });
        }
        write_heights(elevation_header, "0 0 0 0 0 0 0 0 0 0 0 0");
        { std::ofstream out(directory / "elevation.prj"); out << "EPSG:4326"; }
        rejects([&] { ember::load_elevation(elevation_path, *terrain); });
        const auto bad = directory / "bad.asc";
        std::filesystem::copy_file(source / "terrain.prj", directory / "bad.prj",
                                   std::filesystem::copy_options::overwrite_existing);
        const std::string header = "ncols 2\nnrows 2\nxllcorner 420000\nyllcorner 4590000\ncellsize 10\nNODATA_value 0\n";
        for (const auto& data : {header + "102 104", header + "102 104 145 999", header + "102 104 145 165 102",
                                header + "0 91 98 92", header + "102.5 104 145 165",
                                std::string("ncols 2\nncols 2\nxllcorner 420000\nyllcorner 4590000\ncellsize 10\nNODATA_value 0\n102 104 145 165"),
                                std::string("ncols -2\nnrows 2\nxllcorner 420000\nyllcorner 4590000\ncellsize 10\nNODATA_value 0\n")}) {
            { std::ofstream out(bad); out << data; }
            rejects([&] { ember::load_terrain(bad); });
        }
        for (const auto& replacement : {std::string("cellsize 0"), std::string("cellsize -1"), std::string("cellsize nan")}) {
            auto malformed = header;
            malformed.replace(malformed.find("cellsize 10"), 11, replacement);
            { std::ofstream out(bad); out << malformed << "102 104 145 165"; }
            rejects([&] { ember::load_terrain(bad); });
        }
        { std::ofstream out(bad); out << header << "102 104 145 165"; }
        { std::ofstream out(directory / "bad.prj"); out << "EPSG:4326"; }
        rejects([&] { ember::load_terrain(bad); });
        config.terrain_path = fixture.string();
        config.scenarios = 2;
        config.output_directory = directory.string();
        config.export_format = ember::ExportFormat::Both;
        const auto batch = ember::run_batch(config);
        CHECK(batch.completed_scenarios == 2 && batch.width == 4 && batch.height == 3);
        CHECK(batch.terrain_load_seconds == 0); // already loaded once
        CHECK(batch.scenario_results[0].burned_cells == stats.burned_cells);
        CHECK(std::filesystem::file_size(directory / "run.json") > 0);
        std::filesystem::remove_all(directory);
        std::cout << "[PASS] terrain loader, geometry, sharing, ignition, determinism, barriers, metrics, CLI and exports\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
