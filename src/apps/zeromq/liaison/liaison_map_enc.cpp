// Copyright 2026:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include <algorithm> // for sort
#include <array>     // for array
#include <cctype>    // for isalnum, tolower
#include <cstdio>    // for popen, pclose, fgets
#include <cstdlib>   // for getenv
#include <fstream>   // for ifstream, ofstream
#include <regex>     // for regex_search
#include <stdexcept> // for runtime_error
#include <utility>   // for pair

#include <sys/wait.h> // for WEXITSTATUS

#include "goby/util/debug_logger.h" // for glog

#include "liaison_map_enc.h"

namespace fs = std::filesystem;
using goby::glog;

namespace
{
// the chart layers the Map tab draws, in S-57 object class names
const std::array<const char*, 19> drawn_layers{
    "DEPARE", "DRGARE", "LNDARE", "DEPCNT", "COALNE", "SOUNDG", "OBSTRN",
    "WRECKS", "UWTROC", "LIGHTS", "BOYLAT", "BOYCAR", "BOYSAW", "BOYSPP",
    "BOYISD", "BCNLAT", "BCNCAR", "BCNSPP", "BCNISD"};

// one point per sounding, with its depth as an attribute rather than a third coordinate
const char* s57_options = "OGR_S57_OPTIONS='RETURN_PRIMITIVES=OFF,RETURN_LINKAGES=OFF,"
                          "LNAM_REFS=OFF,SPLIT_MULTIPOINT=ON,ADD_SOUNDG_DEPTH=ON'";

std::string shell_quote(const std::string& s)
{
    std::string quoted = "'";
    for (char c : s)
    {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    return quoted + "'";
}

// runs a shell command, returning its exit status and standard output
std::pair<int, std::string> run(const std::string& command)
{
    glog.is_debug1() && glog << "Liaison Map ENC: " << command << std::endl;
    std::string output;
    FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");
    if (!pipe)
        return {-1, output};
    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe)) output += buffer.data();
    int status = pclose(pipe);
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, output};
}

bool have_program(const std::string& program)
{
    return run("command -v " + shell_quote(program)).first == 0;
}

std::string safe_name(const std::string& name)
{
    std::string safe;
    for (char c : name) safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') ? c : '_';
    return safe;
}

std::string lower(std::string s)
{
    for (auto& c : s) c = std::tolower(static_cast<unsigned char>(c));
    return s;
}

std::vector<std::string> zip_entries(const fs::path& zip)
{
    std::pair<int, std::string> listing{-1, ""};
    // GDAL 3.11 added the gdal program; unzip covers older installations
    if (have_program("gdal"))
        listing = run("gdal vsi list -R " + shell_quote("/vsizip/" + zip.string()));
    if (listing.first != 0 && have_program("unzip"))
        listing = run("unzip -Z1 " + shell_quote(zip.string()));
    if (listing.first != 0)
        throw std::runtime_error("Could not list the zip file (needs GDAL 3.11 or unzip)");

    std::vector<std::string> entries;
    std::string::size_type start = 0, end;
    while ((end = listing.second.find('\n', start)) != std::string::npos)
    {
        entries.push_back(listing.second.substr(start, end - start));
        start = end + 1;
    }
    return entries;
}

goby::apps::zeromq::enc::Cell convert(const std::string& name, const std::string& source,
                                      const fs::path& cell_dir)
{
    goby::apps::zeromq::enc::Cell cell{name, {}, {}};
    const fs::path out = cell_dir / "geojson";
    fs::remove_all(out);
    fs::create_directories(out);

    for (const char* layer : drawn_layers)
    {
        const fs::path file = out / (std::string(layer) + ".json");
        const auto [status, output] =
            run(std::string(s57_options) +
                " ogr2ogr -f GeoJSON -lco RFC7946=YES -lco COORDINATE_PRECISION=6 " +
                shell_quote(file.string()) + " " + shell_quote(source) + " " + layer);
        // a layer the cell does not have fails and leaves an empty file
        std::error_code ec;
        if (status == 0 && fs::file_size(file, ec) > 0)
            cell.layers[layer] = file;
        else
            fs::remove(file, ec);
    }

    if (cell.layers.empty())
    {
        fs::remove_all(cell_dir);
        throw std::runtime_error(name + " has no chart layers this map draws");
    }

    // soundings and depth areas are in whatever units the cell declares
    const auto dsid =
        run(std::string(s57_options) + " ogrinfo -ro -q -al " + shell_quote(source) + " DSID");
    std::smatch duni;
    if (std::regex_search(dsid.second, duni, std::regex("DSPM_DUNI \\(Integer\\) = ([0-9]+)")))
    {
        const std::map<std::string, std::string> units{
            {"1", "metres"}, {"2", "fathoms and feet"}, {"3", "feet"}, {"4", "fathoms"}};
        auto it = units.find(duni[1]);
        if (it != units.end())
        {
            cell.depth_units = it->second;
            std::ofstream(cell_dir / "depth_units") << cell.depth_units;
        }
    }
    return cell;
}
} // namespace

fs::path goby::apps::zeromq::enc::directory(const protobuf::ProtobufMapConfig& cfg)
{
    if (cfg.has_enc_dir())
        return cfg.enc_dir();
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / ".goby" / "liaison" / "enc";
}

std::vector<goby::apps::zeromq::enc::Cell>
goby::apps::zeromq::enc::stored_cells(const fs::path& dir)
{
    std::vector<Cell> cells;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        const fs::path geojson = entry.path() / "geojson";
        if (!fs::is_directory(geojson, ec))
            continue;
        Cell cell{entry.path().filename().string(), {}, {}};
        std::ifstream units(entry.path() / "depth_units");
        std::getline(units, cell.depth_units);
        for (const auto& file : fs::directory_iterator(geojson, ec))
        {
            if (file.path().extension() == ".json")
                cell.layers[file.path().stem().string()] = file.path();
        }
        if (!cell.layers.empty())
            cells.push_back(cell);
    }
    std::sort(cells.begin(), cells.end(),
              [](const Cell& a, const Cell& b) { return a.name < b.name; });
    return cells;
}

std::vector<goby::apps::zeromq::enc::Cell>
goby::apps::zeromq::enc::import(const fs::path& upload, const std::string& client_file_name,
                                const fs::path& dir)
{
    if (!have_program("ogr2ogr"))
        throw std::runtime_error("Adding an ENC needs GDAL's ogr2ogr (e.g. apt install gdal-bin)");

    const fs::path client = safe_name(fs::path(client_file_name).filename().string());
    const std::string extension = lower(client.extension().string());

    fs::create_directories(dir);
    std::vector<Cell> cells;

    if (extension == ".000")
    {
        const std::string name = client.stem().string();
        const fs::path cell_dir = dir / name;
        fs::create_directories(cell_dir);
        const fs::path stored = cell_dir / client;
        fs::copy_file(upload, stored, fs::copy_options::overwrite_existing);
        cells.push_back(convert(name, stored.string(), cell_dir));
    }
    else if (extension == ".zip")
    {
        // kept whole: a cell's updates (.001, ...) are read from beside it in the zip
        const fs::path stored = dir / client;
        fs::copy_file(upload, stored, fs::copy_options::overwrite_existing);
        for (const auto& entry : zip_entries(stored))
        {
            if (lower(fs::path(entry).extension().string()) != ".000")
                continue;
            const std::string name = safe_name(fs::path(entry).stem().string());
            cells.push_back(convert(name, "/vsizip/" + stored.string() + "/" + entry, dir / name));
        }
        if (cells.empty())
        {
            fs::remove(stored);
            throw std::runtime_error(client.string() + " holds no S-57 cell (.000 file)");
        }
    }
    else
    {
        throw std::runtime_error("An ENC is an S-57 .000 file, or a .zip of them");
    }
    return cells;
}
