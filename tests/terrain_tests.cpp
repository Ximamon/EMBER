#include "ember/terrain.hpp"
#include "ember/simulation.hpp"
#include "ember/runner.hpp"
#include "ember/cli.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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
