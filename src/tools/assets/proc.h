// ================================================================================================
// File: proc.h
// Brief: Read dmap's .proc render geometry (text mapProcFile003, or id's binary .bproc cache): area
// models, inter-area portals and the area BSP, the input of the world cook. Checked as strictly as
// id's loader would need.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace assets
{

// One ( x y z s t nx ny nz ) record, as idRenderWorldLocal::ParseModel reads it.
struct ProcVertex
{
    std::array<float, 3> position = {};
    std::array<float, 2> uv = {};
    std::array<float, 3> normal = {};
};

struct ProcSurface
{
    std::string material;
    std::vector<ProcVertex> vertices;
    // Triangles wound clockwise seen from the front (idPlane::FromPoints).
    std::vector<std::uint32_t> indices;
};

struct ProcModel
{
    std::string name;
    std::vector<ProcSurface> surfaces;
};

struct ProcPortal
{
    // The winding faces the first area, from which the second is seen through it.
    int areas[2] = {};
    std::vector<std::array<float, 3>> points;
};

struct ProcNode
{
    // Distance = n.p + d; children[0] takes the positive side. A child > 0 is a node index, 0 is
    // solid and < 0 is area -1 - child.
    std::array<float, 4> plane = {};
    int children[2] = {};
};

struct ProcFile
{
    // Every model in file order; area N is the model named "_areaN".
    std::vector<ProcModel> models;
    int areaCount = 0;
    std::vector<ProcPortal> portals;
    std::vector<ProcNode> nodes;
    // Shadow models are parsed and dropped (static shadow volumes are a later gate).
    int shadowModelCount = 0;
};

// Surfaces keep at most 65,535 vertices (16-bit indices on the PS2). Every area 0..N-1 named by
// the portal section must have its model, and node children must stay in range.
bool ParseProc(std::string_view text, ProcFile & output, std::string & error);

// The same geometry from the binary cache retail containers ship (generated/maps/<map>.bproc),
// parsed to its exact end under the same checks. Vertex positions, texture coordinates and normals
// are kept; colours (garbage in the retail _area vertices), tangents and the precomputed shadow and
// silhouette data are skipped. Prelight shadow models are counted, as the text parser does.
bool ParseBinaryProc(std::span<const std::uint8_t> data, ProcFile & output, std::string & error);

// The model of area 'area', or null.
const ProcModel * FindAreaModel(const ProcFile & proc, int area);

} // namespace assets
