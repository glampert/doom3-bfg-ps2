// ================================================================================================
// File: map_tests.cpp
// Brief: The D3ME map cook against the target's validator: a parsed .map's entities round trip with
// their keys' spelling, repeated keys and stripped trailing whitespace; every corrupted field is
// refused; and the geometry CRC readers take the retail .bcm and .aas headers' layouts.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/bytes.h"
#include "assets/map_cook.h"
#include "assets/map_entities.h"
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

} // namespace

int main()
{
    bool passed = RoundTrip();
    passed = Refusals() && passed;
    passed = CrcReaders() && passed;

    if (!passed)
    {
        return 1;
    }

    std::puts("Map format tests passed.");
    return 0;
}
