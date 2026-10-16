// ================================================================================================
// File: main.cpp
// Brief: Inspect and safely stream native Doom resource containers without engine startup.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

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
static constexpr std::uint32_t kMagic = 0xD000000D;
static constexpr std::uint32_t kMaxTableBytes = 32 * 1024 * 1024;
static constexpr std::uint32_t kMaxEntries = 1000000;
static constexpr std::size_t kCopyBytes = 64 * 1024;
static std::uint64_t s_tempSequence = 0;

struct Entry
{
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    std::size_t container = 0;
};

struct Container
{
    fs::path path;
    std::ifstream stream;
    std::uintmax_t bytes = 0;
    fs::file_time_type modified{};
    std::vector<Entry> entries;
};

static bool Fail(const std::string & message)
{
    std::cerr << "resource_unpack: " << message << '\n';
    return false;
}

static std::uint32_t ReadBig(const unsigned char * bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}

static std::uint32_t ReadLittle(const unsigned char * bytes)
{
    return (static_cast<std::uint32_t>(bytes[3]) << 24) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) | static_cast<std::uint32_t>(bytes[0]);
}

static bool ReadExact(std::ifstream & stream, void * buffer, std::size_t bytes)
{
    stream.read(static_cast<char *>(buffer), static_cast<std::streamsize>(bytes));
    return stream.gcount() == static_cast<std::streamsize>(bytes) && !stream.bad();
}

static bool LogicalName(std::string & name)
{
    if (name.empty() || name.size() > 255)
    {
        return false;
    }
    for (char & character : name)
    {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 32 || byte > 126 || character == ':')
        {
            return false;
        }
        if (character == '\\')
        {
            character = '/';
        }
        else if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    if (name.front() == '/' || name.back() == '/')
    {
        return false;
    }
    // Native tables contain a few internal repeated separators. Filesystems
    // collapse these aliases, so select duplicates after that normalization.
    name.erase(std::unique(name.begin(), name.end(), [](char first, char second)
    {
        return first == '/' && second == '/';
    }), name.end());
    std::size_t start = 0;
    while (start <= name.size())
    {
        const std::size_t end = name.find('/', start);
        const std::string part = name.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ')
        {
            return false;
        }
        // Avoid Windows device aliases when the same tool is built on that host.
        const std::string stem = part.substr(0, part.find('.'));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
             stem[3] >= '1' && stem[3] <= '9'))
        {
            return false;
        }
        if (end == std::string::npos)
        {
            return true;
        }
        start = end + 1;
    }
    return false;
}

static bool ParseContainer(const fs::path & input, std::size_t index, Container & container)
{
    std::error_code error;
    container.path = fs::canonical(input, error);
    if (error || !fs::is_regular_file(container.path, error) || error)
    {
        return Fail("input must be a readable regular file: " + input.string());
    }
    container.bytes = fs::file_size(container.path, error);
    if (error)
    {
        return Fail("cannot stat input: " + input.string());
    }
    container.modified = fs::last_write_time(container.path, error);
    if (error)
    {
        return Fail("cannot stat input timestamp: " + input.string());
    }
    container.stream.open(container.path, std::ios::binary);
    std::array<unsigned char, 12> header{};
    if (!container.stream || !ReadExact(container.stream, header.data(), header.size()))
    {
        return Fail("truncated resource header: " + input.string());
    }
    const std::uint32_t tableOffset = ReadBig(header.data() + 4);
    const std::uint32_t tableBytes = ReadBig(header.data() + 8);
    if (ReadBig(header.data()) != kMagic || tableOffset < 12 || tableOffset > 0x7FFFFFFF ||
        tableOffset > container.bytes || tableBytes < 4 || tableBytes > kMaxTableBytes ||
        tableBytes > container.bytes - tableOffset)
    {
        return Fail("invalid resource magic or table bounds: " + input.string());
    }
    container.stream.seekg(static_cast<std::streamoff>(tableOffset));
    std::vector<unsigned char> table(tableBytes);
    if (!container.stream || !ReadExact(container.stream, table.data(), table.size()))
    {
        return Fail("short resource table read: " + input.string());
    }
    const std::uint32_t count = ReadBig(table.data());
    if (count > kMaxEntries || count > (tableBytes - 4) / 13)
    {
        return Fail("invalid resource entry count: " + input.string());
    }
    container.entries.reserve(count);
    std::size_t cursor = 4;
    for (std::uint32_t record = 0; record < count; ++record)
    {
        if (table.size() - cursor < 4)
        {
            return Fail("truncated resource name length: " + input.string());
        }
        const std::uint32_t nameBytes = ReadLittle(table.data() + cursor);
        cursor += 4;
        if (nameBytes == 0 || nameBytes > 255 || nameBytes > table.size() - cursor ||
            table.size() - cursor - nameBytes < 8)
        {
            return Fail("invalid resource name length: " + input.string());
        }
        Entry entry;
        entry.name.assign(reinterpret_cast<const char *>(table.data() + cursor), nameBytes);
        cursor += nameBytes;
        if (!LogicalName(entry.name))
        {
            return Fail("unsafe relative resource path in input: " + input.string());
        }
        entry.offset = ReadBig(table.data() + cursor);
        entry.length = ReadBig(table.data() + cursor + 4);
        entry.container = index;
        cursor += 8;
        const std::uint64_t end = static_cast<std::uint64_t>(entry.offset) + entry.length;
        const std::uint64_t tableEnd = static_cast<std::uint64_t>(tableOffset) + tableBytes;
        if (entry.offset < 12 || entry.offset > 0x7FFFFFFF || entry.length > 0x7FFFFFFF ||
            entry.offset > container.bytes || entry.length > container.bytes - entry.offset ||
            (entry.length && entry.offset < tableEnd && end > tableOffset))
        {
            return Fail("resource payload outside file or overlapping table: " + entry.name);
        }
        container.entries.push_back(std::move(entry));
    }
    if (cursor != table.size())
    {
        return Fail("resource table has trailing bytes: " + input.string());
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> spans;
    spans.reserve(container.entries.size());
    for (const Entry & entry : container.entries)
    {
        if (entry.length)
        {
            spans.emplace_back(entry.offset, entry.length);
        }
    }
    std::sort(spans.begin(), spans.end());
    for (std::size_t i = 1; i < spans.size(); ++i)
    {
        if (spans[i] != spans[i - 1] && static_cast<std::uint64_t>(spans[i - 1].first) + spans[i - 1].second > spans[i].first)
        {
            return Fail("resource payloads partially overlap: " + input.string());
        }
    }
    return true;
}

static bool Unchanged(const Container & container)
{
    std::error_code error;
    const std::uintmax_t bytes = fs::file_size(container.path, error);
    if (error || bytes != container.bytes)
    {
        return Fail("input changed during extraction: " + container.path.string());
    }
    const fs::file_time_type modified = fs::last_write_time(container.path, error);
    if (error || modified != container.modified)
    {
        return Fail("input changed during extraction: " + container.path.string());
    }
    return true;
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

static bool CopyEntry(Container & container, const Entry & entry, const fs::path & root, bool overwrite)
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
    container.stream.clear();
    container.stream.seekg(static_cast<std::streamoff>(entry.offset));
    std::array<char, kCopyBytes> buffer{};
    std::uint32_t remaining = entry.length;
    bool valid = static_cast<bool>(container.stream);
    while (remaining && valid)
    {
        const std::size_t count = std::min(static_cast<std::size_t>(remaining), buffer.size());
        valid = ReadExact(container.stream, buffer.data(), count) && std::fwrite(buffer.data(), 1, count, output) == count;
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
    std::vector<Container> containers(inputs.size());
    std::map<std::string, Entry> selected;
    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
        if (!ParseContainer(inputs[index], index, containers[index]))
        {
            return 1;
        }
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (containers[previous].path == containers[index].path)
            {
                Fail("same input container supplied twice");
                return 1;
            }
        }
        for (const Entry & entry : containers[index].entries)
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
            std::cout << name << '\t' << entry.length << '\t' << containers[entry.container].path.filename().string() << '\n';
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
    for (const Container & container : containers)
    {
        if (IsAncestor(output, container.path))
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
        for (const Container & container : containers)
        {
            const bool same = fs::equivalent(destination, container.path, error);
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
