// ================================================================================================
// File: map_tests.cpp
// Brief: The D3ME map cook against the target's validator: a parsed .map's entities round trip with
// their keys' spelling, repeated keys and stripped trailing whitespace; every corrupted field is
// refused; and the geometry CRC readers take the retail .bcm and .aas headers' layouts. The D3AS
// AAS cook: the authored headless map's AAS cooks to the tables id's parser would build, a special
// reachability keeps its pairs, and the target refuses corrupted or inconsistent files.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/aas_cook.h"
#include "assets/bytes.h"
#include "assets/map_cook.h"
#include "assets/map_entities.h"
#include "ps2/game/aas_format.h"
#include "ps2/game/map_format.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace
{

using namespace assets;
namespace map = ps2::game::map;
namespace aas = ps2::game::aas;

alignas(16) static std::uint8_t s_aligned[64 * 1024];

static bool Check(bool passed, const char * name)
{
    if (!passed)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }

    return passed;
}

static bool TargetValidate(const std::vector<std::uint8_t> & file, map::View & view, const char *& error)
{
    if (file.size() > sizeof(s_aligned))
    {
        error = "file larger than the test buffer";
        return false;
    }

    std::memcpy(s_aligned, file.data(), file.size());
    return map::Validate(s_aligned, file.size(), view, error);
}

// Two entities as a level editor writes them: a worldspawn with a brush, and a light whose key is
// spelt in capitals, repeated, and followed by trailing spaces in its value.
static const char kMapText[] = "Version 3\n"
                               "// entity 0\n"
                               "{\n"
                               "\"classname\" \"worldspawn\"\n"
                               "// primitive 0\n"
                               "{\n"
                               " brushDef3\n"
                               " {\n"
                               "  ( 0 0 1 -64 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) \"textures/base_wall/a\" 0 0 0\n"
                               " }\n"
                               "}\n"
                               "}\n"
                               "// entity 1\n"
                               "{\n"
                               "\"classname\" \"light\"\n"
                               "\"Origin\" \"8 16 32  \"\n"
                               "\"name\" \"light_1\"\n"
                               "\"ORIGIN\" \"1 2 3\"\n"
                               "}\n";

static bool RoundTrip()
{
    std::vector<MapEntity> entities;
    std::string error;
    bool passed = Check(ParseMapEntities(kMapText, entities, error) && entities.size() == 2, "the test map parses");

    std::vector<std::uint8_t> file;
    passed = Check(WriteCookedMap(entities, 0x8B89B51CU, file, error), "the cook writes the test map") && passed;

    map::View view;
    const char * targetError = "";
    passed = Check(TargetValidate(file, view, targetError), "the target accepts the cooked map") && passed;

    if (!passed)
    {
        return false;
    }

    passed = Check(view.header->geometryCrc == 0x8B89B51CU && view.header->entityCount == 2 && view.header->pairCount == 5,
                   "the header carries the CRC and the counts");
    passed = Check(view.pairCounts[0] == 1 && view.pairCounts[1] == 4, "each entity keeps its pairs") && passed;

    // The light's pairs in file order, spelt as written, the trailing spaces gone: the target sets
    // them in this order, so "ORIGIN" replaces "Origin"'s value under the first spelling.
    const map::Pair * const light = view.pairs + 1;
    passed = Check(std::strcmp(view.String(light[0].key), "classname") == 0 && std::strcmp(view.String(light[0].value), "light") == 0,
                   "the light's classname") && passed;
    passed = Check(std::strcmp(view.String(light[1].key), "Origin") == 0 && std::strcmp(view.String(light[1].value), "8 16 32") == 0,
                   "a key keeps its spelling and its value loses trailing whitespace") && passed;
    passed = Check(std::strcmp(view.String(light[3].key), "ORIGIN") == 0 && std::strcmp(view.String(light[3].value), "1 2 3") == 0,
                   "a repeated key stays, in order") && passed;
    passed = Check(std::strcmp(view.String(view.pairs[0].value), "worldspawn") == 0, "the worldspawn's classname") && passed;
    passed = Check(view.pairs[0].key == light[0].key, "a string is stored once") && passed;
    return passed;
}

static bool Refusals()
{
    std::vector<MapEntity> entities;
    std::string error;
    std::vector<std::uint8_t> file;

    if (!Check(ParseMapEntities(kMapText, entities, error) && WriteCookedMap(entities, 1U, file, error) && file.size() > map::kHeaderBytes,
               "the refusal fixture cooks"))
    {
        return false;
    }

    // Rewrites the checksum after a change, so the field under test is what the target refuses.
    const auto Reseal = [](std::vector<std::uint8_t> & bytes)
    {
        WriteLe32(bytes.data() + 24, Fnv1a32(std::span<const std::uint8_t>(bytes).subspan(map::kHeaderBytes)));
    };

    const auto Refused = [](const std::vector<std::uint8_t> & bytes)
    {
        map::View view;
        const char * reason = "";
        return !TargetValidate(bytes, view, reason) && view.header == nullptr;
    };

    bool passed = true;
    std::vector<std::uint8_t> bad = file;
    bad[0] ^= 1U;
    passed = Check(Refused(bad), "a wrong magic is refused") && passed;

    bad = file;
    WriteLe32(bad.data() + 4, 2U);
    passed = Check(Refused(bad), "another version is refused") && passed;

    bad = file;
    bad.back() ^= 0x20U;
    passed = Check(Refused(bad), "a changed byte fails the checksum") && passed;

    bad = file;
    bad.pop_back();
    passed = Check(Refused(bad), "a truncated file is refused") && passed;

    bad = file;
    WriteLe32(bad.data() + 12, 0U);
    passed = Check(Refused(bad), "no entities is refused") && passed;

    bad = file;
    WriteLe32(bad.data() + map::kHeaderBytes, 2U);
    Reseal(bad);
    passed = Check(Refused(bad), "pair counts that miss the pairs are refused") && passed;

    bad = file;
    WriteLe32(bad.data() + map::kHeaderBytes + 8U, 0xFFFFU);
    Reseal(bad);
    passed = Check(Refused(bad), "a string outside the block is refused") && passed;

    bad = file;
    bad[bad.size() - 1] = 'x';
    Reseal(bad);
    passed = Check(Refused(bad), "a block not ending in NUL is refused") && passed;

    // The light's first key moved to the worldspawn value's terminating NUL: an empty key.
    bad = file;
    const std::uint32_t worldValue = ReadLe32(bad.data() + map::kHeaderBytes + 8U + 4U);
    const std::uint32_t emptyAt = worldValue + static_cast<std::uint32_t>(std::strlen("worldspawn"));
    WriteLe32(bad.data() + map::kHeaderBytes + 8U + 8U, emptyAt);
    Reseal(bad);
    passed = Check(Refused(bad), "an empty key is refused") && passed;

    map::View view;
    const char * reason = "";
    passed = Check(!map::Validate(s_aligned + 1, file.size(), view, reason), "an unaligned buffer is refused") && passed;
    return passed;
}

static bool CrcReaders()
{
    // The retail mars_city1.bcm's first bytes: 425 models, an empty map name, then the CRC.
    const std::vector<std::uint8_t> bcm = { 0x00, 0x00, 0x01, 0xA9, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x89, 0xB5, 0x1C, 0x02, 0x00 };
    std::uint32_t crc = 0;
    std::string error;
    bool passed = Check(CollisionGeometryCrc(bcm, crc, error) && crc == 0x8B89B51CU, "the .bcm header's CRC");

    const std::vector<std::uint8_t> named = { 0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 'a', 'b', 'c', 0x12, 0x34, 0x56, 0x78 };
    passed = Check(CollisionGeometryCrc(named, crc, error) && crc == 0x12345678U, "a named .bcm's CRC follows the name") && passed;

    const std::vector<std::uint8_t> shortName = { 0x00, 0x00, 0x00, 0x01, 0x09, 0x00, 0x00, 0x00, 'a', 0, 0, 0 };
    passed = Check(!CollisionGeometryCrc(shortName, crc, error), "a name past the file is refused") && passed;

    // The retail mars_city1.aas48's first lines.
    passed = Check(AasGeometryCrc("DewmAAS \"1.07\"\r\n\r\n2341057820\r\n\r\nsettings\r\n", crc, error) && crc == 0x8B89B51CU,
                   "the .aas header's CRC") && passed;
    passed = Check(!AasGeometryCrc("DewmAAS \"1.07\"\r\n\r\nsettings\r\n", crc, error), "an AAS without a CRC is refused") && passed;
    passed = Check(!AasGeometryCrc("Version 3\n", crc, error), "a file that is not AAS is refused") && passed;
    return passed;
}

static bool AasValidate(const std::vector<std::uint8_t> & file, aas::Tables & tables)
{
    if (file.size() > sizeof(s_aligned))
    {
        return false;
    }

    std::memcpy(s_aligned, file.data(), file.size());
    const char * reason = "";
    return aas::Validate(s_aligned, file.size(), tables, reason);
}

// The authored headless map's AAS: its counts, settings (the file's and the ctor's defaults), a
// plane's negated distance, a face's narrowed fields and both reachabilities.
static bool AasRoundTrip()
{
    std::string text;
    std::FILE * const stream = std::fopen("src/tests/data/headless_map/maps/headless.aas48", "rb");

    if (stream != nullptr)
    {
        char chunk[4096];
        std::size_t read = 0;

        while ((read = std::fread(chunk, 1, sizeof(chunk), stream)) > 0)
        {
            text.append(chunk, read);
        }

        std::fclose(stream);
    }

    std::vector<std::uint8_t> file;
    std::uint32_t crc = 0;
    std::string error;

    if (!Check(!text.empty() && CookAas(text, file, crc, error), "the headless AAS cooks"))
    {
        std::fprintf(stderr, "  %s\n", error.c_str());
        return false;
    }

    aas::Tables tables;
    if (!Check(AasValidate(file, tables), "the target accepts the cooked AAS"))
    {
        return false;
    }

    const aas::Header & header = *tables.header;
    bool passed = Check(crc == 1090519040U && header.geometryCrc == crc, "the AAS's CRC");
    passed = Check(header.planeCount == 14 && header.vertexCount == 12 && header.edgeCount == 21 && header.edgeIndexCount == 48 &&
                       header.faceCount == 12 && header.faceIndexCount == 12 && header.areaCount == 3 && header.nodeCount == 8 &&
                       header.portalCount == 1 && header.portalIndexCount == 0 && header.clusterCount == 2 && header.reachCount == 2 &&
                       header.pairCount == 0,
                   "every table's count") &&
             passed;

    const aas::Settings & settings = *tables.settings;
    passed = Check(settings.boundingBoxCount == 1 && settings.boundingBoxes[0][0] == -8.0f && settings.boundingBoxes[0][5] == 32.0f,
                   "the file's bounding box") &&
             passed;
    passed = Check(settings.gravity[2] == -512.0f && settings.maxStepHeight == 16.0f && settings.maxBarrierHeight == 32.0f &&
                       settings.minFloorCos == 0.7f && settings.ttWaterJump == 100 && settings.flags == 0,
                   "the file's settings over the defaults") &&
             passed;
    passed = Check(std::strcmp(tables.strings + settings.fileExtension, "aas48") == 0, "the file extension") && passed;
    passed = Check(tables.planes[2].c == -1.0f && tables.planes[2].d == 96.0f, "a plane's negated distance") && passed;
    passed = Check(tables.faces[4].planeNum == 6 && tables.faces[4].areas[0] == 1 && tables.faces[4].areas[1] == 2 &&
                       tables.faces[4].firstEdge == 12 && tables.faces[4].numEdges == 4,
                   "a face's fields in their order") &&
             passed;
    passed = Check(tables.areas[1].flags == 65 && tables.areas[1].numFaces == 6 && tables.areas[2].firstFace == 6 &&
                       tables.areas[2].clusterAreaNum == 1 && tables.areas[1].reachCount == 1,
                   "an area's fields") &&
             passed;
    passed = Check(tables.reaches[0].travelType == 2 && tables.reaches[0].toAreaNum == 2 && tables.reaches[0].start[0] == -1.0f &&
                       tables.reaches[0].start[2] == 0.25f && tables.reaches[1].toAreaNum == 1 && tables.reaches[1].travelTime == 1,
                   "the reachabilities in file order") &&
             passed;
    passed = Check(tables.nodes[7].children[0] == -2 && tables.nodes[7].children[1] == -1 && tables.clusters[1].numReachableAreas == 2,
                   "a node's area children and a cluster") &&
             passed;
    return passed;
}

// A minimal AAS: two areas, a tree of one node, one special reachability with two pairs. Its
// fields are substituted to make inconsistent variants.
static std::string MinimalAas(const char * reachTo = "1", const char * faceIndex = "1", const char * settings = "")
{
    return std::string("DewmAAS \"1.07\"\n7\nsettings { allowFlyReachabilities = 1 ") + settings +
           " }\n"
           "planes 1 { 0 ( 0 0 1 -0.1 ) }\n"
           "vertices 2 { 0 ( 0 0 0 ) 1 ( 1 0 0 ) }\n"
           "edges 2 { 0 ( 0 0 ) 1 ( 0 1 ) }\n"
           "edgeIndex 1 { 0 ( -1 ) }\n"
           "faces 2 { 0 ( 9 9 9 9 9 99999 ) 1 ( 0 4 1 0 0 1 ) }\n"
           "faceIndex 1 { 0 ( " +
           faceIndex +
           " ) }\n"
           "areas 2 { 0 ( 0 0 0 0 0 0 ) 0 { } 1 ( 1 0 0 1 1 0 ) 1 {\n"
           "  4096 " +
           reachTo +
           " ( 1 2 3 ) ( 4 5 6.5 ) 1 70000\n"
           "  { \"target\" \"door_1\" \"target\" \"door_2\" } } }\n"
           "nodes 2 { 0 ( 0 0 0 ) 1 ( 0 -1 0 ) }\n"
           "portals 0 { }\nportalIndex 0 { }\nclusters 0 { }\n";
}

static bool AasSpecialAndRefusals()
{
    std::vector<std::uint8_t> file;
    std::uint32_t crc = 0;
    std::string error;
    bool passed = Check(CookAas(MinimalAas(), file, crc, error), "the minimal AAS cooks");
    aas::Tables tables;

    if (!Check(passed && AasValidate(file, tables), "the target accepts the minimal AAS"))
    {
        std::fprintf(stderr, "  %s\n", error.c_str());
        return false;
    }

    // Repeated keys stay in order (idDict::Set takes the later value); travel time narrows to 16 bits.
    passed = Check(tables.header->pairCount == 2 && tables.reaches[0].pairCount == 2 &&
                       std::strcmp(tables.strings + tables.pairs[1].key, "target") == 0 &&
                       std::strcmp(tables.strings + tables.pairs[1].value, "door_2") == 0 && tables.pairs[0].key == tables.pairs[1].key,
                   "a special reachability's pairs") &&
             passed;
    passed = Check(tables.reaches[0].travelTime == static_cast<std::uint16_t>(70000) && tables.reaches[0].end[2] == 6.5f,
                   "a reachability's narrowed travel time") &&
             passed;
    passed = Check(tables.planes[0].d == 0.1f && tables.settings->flags == aas::kAllowFlyReachabilities, "the nearest float, negated") &&
             passed;

    // The cook refuses what the target would, and what id's parser reads another way.
    passed = Check(!CookAas(MinimalAas("2"), file, crc, error), "a reachability to a missing area is refused") && passed;
    passed = Check(!CookAas(MinimalAas("1", "0"), file, crc, error), "a face index entry of 0 is refused") && passed;
    passed = Check(!CookAas(MinimalAas("1", "1", "noSuchKey = 1"), file, crc, error), "an unknown settings key is refused") && passed;
    passed = Check(!CookAas(MinimalAas("01"), file, crc, error), "an octal-looking number is refused") && passed;
    passed = Check(!CookAas(MinimalAas().substr(0, 200), file, crc, error), "a truncated file is refused") && passed;

    // Corruptions of a cooked file.
    if (!CookAas(MinimalAas(), file, crc, error))
    {
        return false;
    }

    const auto Reseal = [](std::vector<std::uint8_t> & bytes)
    {
        WriteLe32(bytes.data() + 12, Fnv1a32(std::span<const std::uint8_t>(bytes).subspan(sizeof(aas::Header))));
    };

    std::vector<std::uint8_t> bad = file;
    bad.back() ^= 1U;
    passed = Check(!AasValidate(bad, tables), "a changed byte fails the checksum") && passed;

    bad = file;
    bad.pop_back();
    passed = Check(!AasValidate(bad, tables), "a truncated cooked file is refused") && passed;

    bad = file;
    WriteLe32(bad.data() + 4, 2U);
    passed = Check(!AasValidate(bad, tables), "another version is refused") && passed;

    // The reachability's toAreaNum, at byte 4 of the last record.
    bad = file;
    WriteLe16(bad.data() + bad.size() - sizeof(aas::Reach) + 4, 7U);
    Reseal(bad);
    passed = Check(!AasValidate(bad, tables), "a cooked reachability to a missing area is refused") && passed;

    // An Inf in the reachability's start.
    bad = file;
    WriteLe32(bad.data() + bad.size() - sizeof(aas::Reach) + 8, 0x7F800000U);
    Reseal(bad);
    passed = Check(!AasValidate(bad, tables), "a value that is not finite is refused") && passed;
    return passed;
}

} // namespace

int main()
{
    bool passed = RoundTrip();
    passed = Refusals() && passed;
    passed = CrcReaders() && passed;
    passed = AasRoundTrip() && passed;
    passed = AasSpecialAndRefusals() && passed;

    if (!passed)
    {
        return 1;
    }

    std::puts("Map and AAS format tests passed.");
    return 0;
}
