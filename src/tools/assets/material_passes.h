// ================================================================================================
// File: material_passes.h
// Brief: Compile a world material, under the parms its surfaces draw with, into what the PS2 draws:
// the base pass (its diffuse stage's alpha test reference and colour) and the additive passes its
// other stages become, their colours and conditions folded to constants where only parms and
// constants reach them, and to D3TX expression nodes where time does.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "assets/material.h"
#include "assets/proc.h"
#include "assets/texture_pack.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace assets
{

// An additive pass before its texture is cooked: 'image' is the stage's map. Table nodes name their
// table in 'nodeTables' (b and count are set when the pack places the values); every other entry is
// empty. Node operands index 'nodes'.
struct PlannedPass
{
    int stage = -1;
    std::string image;
    D3txPass pass;
    std::vector<D3txNode> nodes;
    std::vector<std::string> nodeTables;
};

struct MaterialPassPlan
{
    // The diffuse stage's alpha test reference on the GS scale (0x80 = 1.0), 0 without a test.
    std::uint8_t alphaRef = 0;
    // Its colour registers at time 0, which the cook folds into the vertex colours.
    std::array<float, 3> diffuseColor = { 1.0f, 1.0f, 1.0f };
    std::vector<PlannedPass> passes;
    // Stages that draw nothing on the PS2 yet, and why.
    std::vector<std::string> skipped;
};

// Plans an opaque or alpha-tested material with a diffuse stage; for any other the plan is empty
// but for its colour. Conditions false under 'parms' drop their stage.
MaterialPassPlan PlanMaterialPasses(const MaterialInfo & material, const ShaderParms & parms, const MaterialTables & tables);

// Appends a planned pass to 'pack' with its cooked 'texture': its nodes after the pack's, their
// tables' values once per table ('tableOffsets' remembers where), and the pass's node indices
// moved with them.
void AddPlannedPass(TexturePack & pack, const PlannedPass & planned, std::uint32_t texture, const MaterialTables & tables,
                    std::map<std::string, std::uint16_t> & tableOffsets);

// Evaluates a planned pass's nodes at 'time' as the target does (texture_format.h's
// EvaluatePassNodes), for the cook's checks and tests.
std::vector<float> EvaluatePlannedNodes(const PlannedPass & pass, const MaterialTables & tables, float time);

// A colour register's value as a colour byte (0x80 = 1.0, at most 0xFF).
std::uint8_t PassColorByte(float value);

} // namespace assets
