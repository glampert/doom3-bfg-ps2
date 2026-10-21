// ================================================================================================
// File: main.cpp
// Brief: Inspect and safely stream native Doom resource containers without engine startup.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/resources.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using assets::ResourceContainer;
using assets::ResourceEntry;
static constexpr std::size_t kCopyBytes = 64 * 1024;
static std::uint64_t s_tempSequence = 0;

static bool Fail(const std::string & message)
{
    std::cerr << "resource_unpack: " << message << '\n';
    return false;
}

static bool ReadExact(std::ifstream & stream, void * buffer, std::size_t bytes)
{
    stream.read(static_cast<char *>(buffer), static_cast<std::streamsize>(bytes));
    return stream.gcount() == static_cast<std::streamsize>(bytes) && !stream.bad();
}

static bool Unchanged(const ResourceContainer & container)
{
    std::string error;
    return container.Unchanged(error) || Fail(error);
}

static bool IsAncestor(const fs::path & ancestor, const fs::path & path)
{
    auto parent = ancestor.begin();
    auto child = path.begin();
    while (parent != ancestor.end() && child != path.end() && *parent == *child)
    {
        ++parent;
        ++child;
    }
    return parent == ancestor.end();
}

static bool Status(const fs::path & path, fs::file_status & status)
{
    std::error_code error;
    status = fs::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return Fail("cannot inspect output path: " + path.string());
    }
    return true;
}

static bool CheckDestination(const fs::path & root, const std::string & name, bool overwrite)
{
    fs::path current = root;
    fs::file_status status;
    if (!Status(root, status))
    {
        return false;
    }
    if (fs::is_symlink(status) || (fs::exists(status) && !fs::is_directory(status)))
    {
        return Fail("output root must be a directory without a symlink: " + root.string());
    }
    const fs::path relative(name);
    for (auto part = relative.begin(); part != relative.end(); ++part)
    {
        current /= *part;
        if (!Status(current, status))
        {
            return false;
        }
        if (fs::is_symlink(status))
        {
            return Fail("symlink in output path: " + current.string());
        }
        const bool last = std::next(part) == relative.end();
        if (fs::exists(status) && ((!last && !fs::is_directory(status)) ||
                                   (last && (!overwrite || !fs::is_regular_file(status)))))
        {
            return Fail("output path already exists or conflicts: " + current.string());
        }
    }
    return true;
}

static bool CopyEntry(ResourceContainer & container, const ResourceEntry & entry, const fs::path & root, bool overwrite)
{
    if (!Unchanged(container) || !CheckDestination(root, entry.name, overwrite))
    {
        return false;
    }
    const fs::path destination = root / entry.name;
    std::error_code error;
    fs::create_directories(destination.parent_path(), error);
    if (error || !CheckDestination(root, entry.name, overwrite))
    {
        return Fail("cannot create safe destination directory: " + destination.parent_path().string());
    }
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path temporary;
    std::FILE * output = nullptr;
    for (unsigned int attempt = 0; attempt < 32 && output == nullptr; ++attempt)
    {
        ++s_tempSequence;
        temporary = destination.parent_path() / (".resource-unpack-" + std::to_string(timestamp) + "-" + std::to_string(s_tempSequence) + ".tmp");
        output = std::fopen(temporary.string().c_str(), "wbx");
    }
    if (output == nullptr)
    {
        return Fail("cannot create exclusive temporary output: " + destination.string());
    }
    std::ifstream & stream = container.Stream();
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(entry.offset));
    std::array<char, kCopyBytes> buffer{};
    std::uint32_t remaining = entry.length;
    bool valid = static_cast<bool>(stream);
    while (remaining && valid)
    {
        const std::size_t count = std::min(static_cast<std::size_t>(remaining), buffer.size());
        valid = ReadExact(stream, buffer.data(), count) && std::fwrite(buffer.data(), 1, count, output) == count;
        remaining -= static_cast<std::uint32_t>(count);
    }
    const bool closed = std::fclose(output) == 0;
    if (!valid || !closed || !Unchanged(container) || !CheckDestination(root, entry.name, overwrite))
    {
        fs::remove(temporary, error);
        return Fail("payload copy failed before publication: " + entry.name);
    }
    if (overwrite)
    {
        fs::rename(temporary, destination, error);
    }
    else
    {
        // A hard-link publish refuses a concurrently appearing file atomically;
        // unlike rename, it cannot replace an existing output on POSIX hosts.
        fs::create_hard_link(temporary, destination, error);
        if (!error)
        {
            fs::remove(temporary, error);
        }
    }
    if (error)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return Fail("cannot publish output without conflict: " + destination.string());
    }
    return true;
}

static void Usage()
{
    std::cout << "Usage: resource_unpack [--list] [--output DIR] [--overwrite] INPUT.resources...\n"
                 "Later input containers and later records win after ASCII case/slash normalization.\n"
                 "Repeated internal separators alias the same output path; leading/trailing separators are refused.\n"
                 "--list validates all inputs and lists the merged names without writing files.\n"
                 "Extraction requires --output; existing files are refused unless --overwrite is explicit.\n";
}

static int Run(int argc, char ** argv)
{
    bool list = false;
    bool overwrite = false;
    fs::path output;
    std::vector<fs::path> inputs;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument(argv[i]);
        if (argument == "--help" || argument == "-h")
        {
            Usage();
            return 0;
        }
        if (argument == "--list")
        {
            list = true;
        }
        else if (argument == "--overwrite")
        {
            overwrite = true;
        }
        else if (argument == "--output" && i + 1 < argc && output.empty())
        {
            output = argv[++i];
        }
        else if (argument.starts_with('-'))
        {
            Fail("unknown or incomplete option: " + argument);
            return 2;
        }
        else
        {
            inputs.emplace_back(argument);
        }
    }
    if (inputs.empty() || inputs.size() > 255 || (!list && output.empty()) || (list && (!output.empty() || overwrite)))
    {
        Usage();
        return 2;
    }
    std::vector<ResourceContainer> containers(inputs.size());
    std::map<std::string, ResourceEntry> selected;
    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
        std::string error;
        if (!containers[index].Open(inputs[index], index, error))
        {
            Fail(error);
            return 1;
        }
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (containers[previous].Path() == containers[index].Path())
            {
                Fail("same input container supplied twice");
                return 1;
            }
        }
        for (const ResourceEntry & entry : containers[index].Entries())
        {
            selected[entry.name] = entry;
        }
    }
    for (const auto & [name, entry] : selected)
    {
        static_cast<void>(entry);
        std::size_t slash = name.find('/');
        while (slash != std::string::npos)
        {
            if (selected.contains(name.substr(0, slash)))
            {
                Fail("resource file/directory path collision: " + name);
                return 1;
            }
            slash = name.find('/', slash + 1);
        }
    }
    if (list)
    {
        for (const auto & [name, entry] : selected)
        {
            std::cout << name << '\t' << entry.length << '\t' << containers[entry.container].Path().filename().string() << '\n';
        }
        return 0;
    }
    fs::file_status rootStatus;
    if (!Status(output, rootStatus) || fs::is_symlink(rootStatus))
    {
        Fail("output root cannot be a symlink");
        return 1;
    }
    // Resolve explicitly chosen parent aliases (e.g. macOS /tmp), then ensure
    // no resource-controlled descendant can use symlinks to leave this root.
    std::error_code error;
    output = fs::weakly_canonical(output, error);
    if (error)
    {
        Fail("cannot resolve output directory");
        return 1;
    }
    for (const ResourceContainer & container : containers)
    {
        if (IsAncestor(output, container.Path()))
        {
            Fail("output directory must not contain an input container");
            return 1;
        }
    }
    // Validate every destination before creating any directory or payload.
    for (const auto & [name, entry] : selected)
    {
        static_cast<void>(entry);
        if (!CheckDestination(output, name, overwrite))
        {
            return 1;
        }
        const fs::path destination = output / name;
        for (const ResourceContainer & container : containers)
        {
            const bool same = fs::equivalent(destination, container.Path(), error);
            if (!error && same)
            {
                Fail("output aliases an input container");
                return 1;
            }
            error.clear();
        }
    }
    std::uint64_t copiedBytes = 0;
    for (const auto & [name, entry] : selected)
    {
        static_cast<void>(name);
        if (!CopyEntry(containers[entry.container], entry, output, overwrite))
        {
            return 1;
        }
        copiedBytes += entry.length;
    }
    std::cout << "Extracted " << selected.size() << " files (" << copiedBytes << " bytes) into " << output.string() << '\n';
    return 0;
}
} // namespace

int main(int argc, char ** argv)
{
    const int result = Run(argc, argv);
    std::cout.flush();
    if (result == 0 && !std::cout)
    {
        Fail("cannot write standard output");
        return 1;
    }
    return result;
}
