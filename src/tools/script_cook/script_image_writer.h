// ================================================================================================
// File: script_image_writer.h
// Brief: Turns a program id's compiler has just built into the cooked image the PS2 loads
// (ps2/game/script_image_format.h): only what the interpreter and the game's lookups read.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class idProgram;
class idEventDef;

namespace cook
{

// Section sizes of a written image, for the cook's report.
struct ImageReport
{
    std::uint32_t fileBytes = 0;
    std::uint32_t residentBytes = 0;
    std::uint32_t buildBytes = 0;
    std::uint32_t debugBytes = 0;
    std::uint32_t defs = 0;
    std::uint32_t statements = 0;
    std::uint32_t lookups = 0;
    std::uint32_t scopes = 0;
    std::uint32_t functions = 0;
    std::uint32_t types = 0;
    std::uint32_t events = 0;
    std::uint32_t stringBytes = 0;
    std::uint32_t globalBytes = 0;
    std::uint32_t compiledGlobalBytes = 0;
};

class ImageWriter final
{
public:
    // threadResponds answers whether idThread handles an event (a sys call), as the compiler's
    // own query did. Returns false with a reason when the program does not fit the format.
    static bool Write(const idProgram & program, bool (*threadResponds)(const idEventDef & event), std::vector<std::uint8_t> & image,
                      ImageReport & report, std::string & error);
};

} // namespace cook
