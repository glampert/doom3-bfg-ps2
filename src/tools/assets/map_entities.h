// ================================================================================================
// File: map_entities.h
// Brief: Read the entities of a Doom 3 .map (version 2 or 3): each entity's key/value pairs, with
// its brush and patch primitives counted and skipped, which is what the light cook needs from it.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace assets
{

// Keys are lowercase (idDict compares them without case); values keep their text.
struct MapEntity
{
    std::vector<std::pair<std::string, std::string> > keys;
    // The pairs as the file spells them, in its order: keys keep their case and repeated keys stay,
    // for the map cook, whose output idMapEntity sets in this order as id's parser does.
    std::vector<std::pair<std::string, std::string> > written;
    int primitives = 0;

    // The value of 'key' (lowercase), or null.
    const std::string * Find(std::string_view key) const;

    // idDict::GetString/GetFloat/GetInt/GetBool/GetVector: 'fallback' when the key is absent or
    // its value does not parse (a vector takes three numbers).
    std::string String(std::string_view key, std::string_view fallback = {}) const;
    float Float(std::string_view key, float fallback) const;
    int Int(std::string_view key, int fallback) const;
    bool Bool(std::string_view key, bool fallback) const;
    bool Vector(std::string_view key, std::array<float, 3> & output) const;
    // Nine numbers, rows in order (idDict::GetMatrix).
    bool Matrix(std::string_view key, std::array<std::array<float, 3>, 3> & output) const;

    // Where the entity's model sits: 'origin', and the rows of 'rotation', else a yaw of 'angle'
    // degrees (idGameEdit::ParseSpawnArgsToRenderEntity). A point p of the model lands at
    // origin + p[0] * axis[0] + p[1] * axis[1] + p[2] * axis[2].
    void Placement(std::array<float, 3> & origin, std::array<std::array<float, 3>, 3> & axis) const;
};

// The entities in file order; the first is worldspawn. Comments, quoted strings and nesting are
// checked; a primitive's contents are not parsed.
bool ParseMapEntities(std::string_view text, std::vector<MapEntity> & output, std::string & error);

} // namespace assets
