#include "ember/terrain.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace ember {
bool known_fuel_code(int code) noexcept {
    switch (code) {
    case 0: case 91: case 92: case 93: case 98:
    case 102: case 104: case 106: case 107: case 108: case 109:
    case 142: case 143: case 145: case 147: case 148: case 149:
    case 161: case 162: case 163: case 165: case 183: return true;
    default: return false;
    }
}
bool combustible_code(int code) noexcept { return known_fuel_code(code) && code >= 100; }

std::shared_ptr<const TerrainData> load_terrain(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::invalid_argument("could not open terrain: " + path.string());
    std::map<std::string, double> header;
    for (int i = 0; i < 6; ++i) {
        std::string line, key, extra;
        double value = 0;
        if (!std::getline(input, line)) throw std::invalid_argument("truncated terrain header");
        std::istringstream row(line);
        if (!(row >> key >> value) || (row >> extra) || !std::isfinite(value))
            throw std::invalid_argument("invalid terrain header");
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (!header.emplace(key, value).second) throw std::invalid_argument("duplicate terrain header");
    }
    for (const auto* key : {"ncols", "nrows", "xllcorner", "yllcorner", "cellsize", "nodata_value"})
        if (!header.count(key)) throw std::invalid_argument(std::string("missing terrain header: ") + key);
    auto dimension = [](double n) {
        // Bound both conversion and allocations for this local demo format.
        if (n < 1 || n > 100000 || std::floor(n) != n)
            throw std::invalid_argument("invalid terrain dimensions");
        return static_cast<std::size_t>(n);
    };
    auto terrain = std::make_shared<TerrainData>();
    terrain->width = dimension(header.at("ncols"));
    terrain->height = dimension(header.at("nrows"));
    if (terrain->width > 16000000 / terrain->height)
        throw std::invalid_argument("terrain exceeds 16 million cell limit; prepare a smaller crop");
    terrain->xllcorner = header.at("xllcorner");
    terrain->yllcorner = header.at("yllcorner");
    terrain->cell_size_m = header.at("cellsize");
    if (terrain->cell_size_m <= 0 || header.at("nodata_value") != 0)
        throw std::invalid_argument("terrain requires positive cellsize and NODATA_value 0");
    const double east = terrain->xllcorner + static_cast<double>(terrain->width) * terrain->cell_size_m;
    const double north = terrain->yllcorner + static_cast<double>(terrain->height) * terrain->cell_size_m;
    if (!std::isfinite(east) || !std::isfinite(north) || terrain->xllcorner < 100000 || east > 900000 ||
        terrain->yllcorner < 0 || north > 10000000)
        throw std::invalid_argument("invalid UTM terrain extent");
    auto projection_path = path;
    projection_path.replace_extension(".prj");
    std::ifstream projection(projection_path);
    std::string crs((std::istreambuf_iterator<char>(projection)), std::istreambuf_iterator<char>());
    // v1 deliberately supports only the demo's metric CRS. The preparer writes this exact WKT.
    if (crs.find("AUTHORITY[\"EPSG\",\"32631\"]") == std::string::npos &&
        crs.find("ID[\"EPSG\",32631]") == std::string::npos)
        throw std::invalid_argument("terrain needs a .prj sidecar with EPSG:32631 (metres)");
    const auto count = terrain->width * terrain->height;
    terrain->codes.reserve(count);
    terrain->valid.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        int code = -1;
        if (!(input >> code) || !known_fuel_code(code))
            throw std::invalid_argument("invalid, unknown or missing fuel code at cell " + std::to_string(i));
        terrain->codes.push_back(static_cast<std::uint16_t>(code));
        terrain->valid.push_back(static_cast<std::uint8_t>(code != 0));
        terrain->valid_cells += code != 0 ? 1U : 0U;
        terrain->combustible_cells += combustible_code(code) ? 1U : 0U;
    }
    std::string extra;
    if (input >> extra) throw std::invalid_argument("extra data after terrain cells");
    if (!terrain->combustible_cells) throw std::invalid_argument("terrain contains no combustible cells");
    return terrain;
}

SimulationConfig resolve_terrain_config(SimulationConfig config) {
#if EMBER_ENABLE_CUDA
    if (!config.terrain_path.empty() || config.terrain)
        throw std::invalid_argument("real terrain requires a CPU build: EMBER_ENABLE_CUDA=OFF");
#endif
    if (!config.terrain && !config.terrain_path.empty()) config.terrain = load_terrain(config.terrain_path);
    if (config.terrain) {
        const auto& t = *config.terrain;
        if ((config.width_explicit && config.width != t.width) ||
            (config.height_explicit && config.height != t.height))
            throw std::invalid_argument("explicit grid dimensions conflict with terrain");
        config.width = t.width;
        config.height = t.height;
        if (config.ignitions.empty()) {
            double best = std::numeric_limits<double>::infinity();
            IgnitionPoint point;
            const double cx = (static_cast<double>(t.width) - 1) / 2;
            const double cy = (static_cast<double>(t.height) - 1) / 2;
            for (std::size_t i = 0; i < t.codes.size(); ++i) {
                if (!combustible_code(t.codes[i])) continue;
                const double dx = static_cast<double>(i % t.width) - cx;
                const double dy = static_cast<double>(i / t.width) - cy;
                const double d = dx * dx + dy * dy;
                if (d < best) { best = d; point = {i % t.width, i / t.width}; }
            }
            config.ignitions.push_back(point);
        }
        for (const auto& point : config.ignitions) {
            if (point.x >= t.width || point.y >= t.height ||
                !combustible_code(t.codes[point.y * t.width + point.x]))
                throw std::invalid_argument("ignition must be inside a combustible terrain cell");
        }
    }
    validate_config(config);
    return config;
}
} // namespace ember
