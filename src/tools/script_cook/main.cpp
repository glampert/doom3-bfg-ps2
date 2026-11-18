// ================================================================================================
// File: main.cpp
// Brief: script_cook: compiles the retail game script (script/doom_main.script and its includes,
// read from the retail containers) with id's own compiler, so the PS2 loads the finished program
// instead of spending about nine seconds and 4.9 MB of heap compiling it at every boot.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

// The standard headers come first: idlib's precompiled.h redefines printf-family names.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "assets/resources.h"

#include "idlib/precompiled.h"

#include "tools/script_cook/cook_engine.h"
#include "tools/script_cook/cook_events.h"
#include "tools/script_cook/cook_game.h"
#include "tools/script_cook/script_image_writer.h"

namespace
{

static assets::ResourceCatalog s_catalog;
static std::vector<std::filesystem::path> s_sourceDirectories;

bool ReadBytes(const std::filesystem::path & path, std::vector<std::uint8_t> & data)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
    {
        return false;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return false;
    }

    data.resize(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(size));
    return static_cast<std::uintmax_t>(stream.gcount()) == size;
}

// Loose source folders first (an authored fixture), then the containers, as the game's search.
bool ReadSource(const char * name, std::vector<std::uint8_t> & data)
{
    std::string normalized = name;
    if (!assets::NormalizeResourceName(normalized))
    {
        return false;
    }

    for (const std::filesystem::path & directory : s_sourceDirectories)
    {
        const std::filesystem::path path = directory / normalized;
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error))
        {
            return ReadBytes(path, data);
        }
    }

    if (s_catalog.Find(normalized) == nullptr)
    {
        return false;
    }

    std::string error;
    if (!s_catalog.Read(normalized, data, error))
    {
        std::fprintf(stderr, "script_cook: %s\n", error.c_str());
        return false;
    }

    return true;
}

bool ThreadResponds(const idEventDef & event)
{
    return idThread::Type.RespondsTo(event);
}

bool WriteFile(const std::filesystem::path & path, const std::vector<std::uint8_t> & bytes)
{
    std::error_code error;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), error);
    }

    // Written beside the target and renamed over it, so a failed cook never leaves half an image.
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }

        stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream)
        {
            return false;
        }
    }

    std::filesystem::rename(temporary, path, error);
    return !error;
}

bool ReadText(const std::filesystem::path & path, std::string & text)
{
    std::vector<std::uint8_t> data;
    if (!ReadBytes(path, data))
    {
        return false;
    }

    text.assign(data.begin(), data.end());
    return true;
}

void Usage()
{
    std::fprintf(stderr,
                 "usage: script_cook --events <events.txt> (--resources <container> | --source-dir <folder>)...\n"
                 "                   --output <program.d3sp> [--script <name>] [--verbose]\n");
}

} // namespace

int main(int argc, char ** argv)
{
    std::vector<std::filesystem::path> containers;
    std::filesystem::path eventsPath;
    std::filesystem::path outputPath;
    std::string script = "script/doom_main.script";
    bool verbose = false;

    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        const bool hasValue = index + 1 < argc;

        if (argument == "--resources" && hasValue)
        {
            containers.emplace_back(argv[++index]);
        }
        else if (argument == "--source-dir" && hasValue)
        {
            s_sourceDirectories.emplace_back(argv[++index]);
        }
        else if (argument == "--events" && hasValue)
        {
            eventsPath = argv[++index];
        }
        else if (argument == "--output" && hasValue)
        {
            outputPath = argv[++index];
        }
        else if (argument == "--script" && hasValue)
        {
            script = argv[++index];
        }
        else if (argument == "--verbose")
        {
            verbose = true;
        }
        else
        {
            Usage();
            return 2;
        }
    }

    if ((containers.empty() && s_sourceDirectories.empty()) || eventsPath.empty() || outputPath.empty())
    {
        Usage();
        return 2;
    }

    std::string error;
    if (!containers.empty() && !s_catalog.Open(containers, error))
    {
        std::fprintf(stderr, "script_cook: %s\n", error.c_str());
        return 1;
    }

    std::string events;
    if (!ReadText(eventsPath, events))
    {
        std::fprintf(stderr, "script_cook: cannot read %s\n", eventsPath.string().c_str());
        return 1;
    }

    if (!cook::LoadEvents(events, error))
    {
        std::fprintf(stderr, "script_cook: %s\n", error.c_str());
        return 1;
    }

    cook::InitEngine(ReadSource, verbose);
    gameLocal.program.Startup(script.c_str());

    std::vector<std::uint8_t> image;
    cook::ImageReport report;
    if (!cook::ImageWriter::Write(gameLocal.program, ThreadResponds, image, report, error))
    {
        std::fprintf(stderr, "script_cook: %s\n", error.c_str());
        return 1;
    }

    if (!WriteFile(outputPath, image))
    {
        std::fprintf(stderr, "script_cook: cannot write %s\n", outputPath.string().c_str());
        return 1;
    }

    std::printf("Wrote %s: %u bytes (resident %u, build %u, debug %u)\n", outputPath.string().c_str(), report.fileBytes,
                report.residentBytes, report.buildBytes, report.debugBytes);
    std::printf("  %u defs, %u statements, %u lookups, %u scopes, %u functions, %u types, %u events\n", report.defs,
                report.statements, report.lookups, report.scopes, report.functions, report.types, report.events);
    std::printf("  %u string bytes, %u global bytes (%u compiled)\n", report.stringBytes, report.globalBytes,
                report.compiledGlobalBytes);
    return 0;
}
