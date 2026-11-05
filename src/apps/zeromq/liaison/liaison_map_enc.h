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

#ifndef GOBY_APPS_ZEROMQ_LIAISON_LIAISON_MAP_ENC_H
#define GOBY_APPS_ZEROMQ_LIAISON_LIAISON_MAP_ENC_H

#include <filesystem> // for path
#include <map>        // for map
#include <string>     // for string
#include <vector>     // for vector

#include "goby/zeromq/protobuf/liaison_config.pb.h" // for ProtobufMapConfig

// S-57 electronic navigational charts (ENCs) stored for the Map tab. Cells are converted to one
// GeoJSON file per chart layer with GDAL's ogr2ogr, run as a program so that Goby does not link
// GDAL.
namespace goby
{
namespace apps
{
namespace zeromq
{
namespace enc
{
struct Cell
{
    std::string name;
    // as the cell declares them (DSPM DUNI), e.g. "metres"; empty if unknown
    std::string depth_units;
    // S-57 object class (e.g. DEPARE) to its GeoJSON file
    std::map<std::string, std::filesystem::path> layers;
};

std::filesystem::path directory(const protobuf::ProtobufMapConfig& cfg);

// cells already converted into the directory, sorted by name
std::vector<Cell> stored_cells(const std::filesystem::path& dir);

// Stores and converts an uploaded .000 cell, or a .zip holding one or more. Throws
// std::runtime_error with a message meant for the person uploading.
std::vector<Cell> import(const std::filesystem::path& upload, const std::string& client_file_name,
                         const std::filesystem::path& dir);

} // namespace enc
} // namespace zeromq
} // namespace apps
} // namespace goby

#endif
