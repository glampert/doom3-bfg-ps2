// ================================================================================================
// File: resources.cpp
// Brief: Validate native Doom 3 BFG .resources containers and read their records, so every host
// tool resolves retail files exactly as the engine's lookup does.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "assets/resources.h"
#include "assets/bytes.h"

#include <algorithm>
#include <array>
#include <system_error>
#include <utility>

namespace assets
{
namespace
{

namespace fs = std::filesystem;

static constexpr std::uint32_t kMagic = 0xD000000D;
static constexpr std::uint32_t kMaxTableBytes = 32 * 1024 * 1024;
static constexpr std::uint32_t kMaxEntries = 1000000;

static bool ReadExact(std::ifstream & stream, void * buffer, std::size_t bytes)
{
    stream.read(static_cast<char *>(buffer), static_cast<std::streamsize>(bytes));
    return stream.gcount() == static_cast<std::streamsize>(bytes) && !stream.bad();
}

} // namespace

bool NormalizeResourceName(std::string & name)
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
                           }),
               name.end());
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
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' &&
             stem[3] <= '9'))
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

bool ResourceContainer::Open(const fs::path & input, std::size_t index, std::string & error)
{
    std::error_code status;
    m_path = fs::canonical(input, status);

    if (status || !fs::is_regular_file(m_path, status) || status)
    {
        error = "input must be a readable regular file: " + input.string();
        return false;
    }

    m_bytes = fs::file_size(m_path, status);

    if (status)
    {
        error = "cannot stat input: " + input.string();
        return false;
    }

    m_modified = fs::last_write_time(m_path, status);

    if (status)
    {
        error = "cannot stat input timestamp: " + input.string();
        return false;
    }

    m_stream.open(m_path, std::ios::binary);
    std::array<std::uint8_t, 12> header{};

    if (!m_stream || !ReadExact(m_stream, header.data(), header.size()))
    {
        error = "truncated resource header: " + input.string();
        return false;
    }

    const std::uint32_t tableOffset = ReadBe32(header.data() + 4);
    const std::uint32_t tableBytes = ReadBe32(header.data() + 8);

    if (ReadBe32(header.data()) != kMagic || tableOffset < 12 || tableOffset > 0x7FFFFFFF || tableOffset > m_bytes ||
        tableBytes < 4 || tableBytes > kMaxTableBytes || tableBytes > m_bytes - tableOffset)
    {
        error = "invalid resource magic or table bounds: " + input.string();
        return false;
    }

    m_stream.seekg(static_cast<std::streamoff>(tableOffset));
    std::vector<std::uint8_t> table(tableBytes);

    if (!m_stream || !ReadExact(m_stream, table.data(), table.size()))
    {
        error = "short resource table read: " + input.string();
        return false;
    }

    const std::uint32_t count = ReadBe32(table.data());

    if (count > kMaxEntries || count > (tableBytes - 4) / 13)
    {
        error = "invalid resource entry count: " + input.string();
        return false;
    }

    m_entries.clear();
    m_entries.reserve(count);
    std::size_t cursor = 4;

    for (std::uint32_t record = 0; record < count; ++record)
    {
        if (table.size() - cursor < 4)
        {
            error = "truncated resource name length: " + input.string();
            return false;
        }

        const std::uint32_t nameBytes = ReadLe32(table.data() + cursor);
        cursor += 4;

        if (nameBytes == 0 || nameBytes > 255 || nameBytes > table.size() - cursor || table.size() - cursor - nameBytes < 8)
        {
            error = "invalid resource name length: " + input.string();
            return false;
        }

        ResourceEntry entry;
        entry.name.assign(reinterpret_cast<const char *>(table.data() + cursor), nameBytes);
        cursor += nameBytes;

        if (!NormalizeResourceName(entry.name))
        {
            error = "unsafe relative resource path in input: " + input.string();
            return false;
        }

        entry.offset = ReadBe32(table.data() + cursor);
        entry.length = ReadBe32(table.data() + cursor + 4);
        entry.container = index;
        cursor += 8;
        const std::uint64_t end = static_cast<std::uint64_t>(entry.offset) + entry.length;
        const std::uint64_t tableEnd = static_cast<std::uint64_t>(tableOffset) + tableBytes;

        if (entry.offset < 12 || entry.offset > 0x7FFFFFFF || entry.length > 0x7FFFFFFF || entry.offset > m_bytes ||
            entry.length > m_bytes - entry.offset || (entry.length && entry.offset < tableEnd && end > tableOffset))
        {
            error = "resource payload outside file or overlapping table: " + entry.name;
            return false;
        }

        m_entries.push_back(std::move(entry));
    }

    if (cursor != table.size())
    {
        error = "resource table has trailing bytes: " + input.string();
        return false;
    }

    std::vector<std::pair<std::uint32_t, std::uint32_t> > spans;
    spans.reserve(m_entries.size());

    for (const ResourceEntry & entry : m_entries)
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
            error = "resource payloads partially overlap: " + input.string();
            return false;
        }
    }

    return true;
}

bool ResourceContainer::Unchanged(std::string & error) const
{
    std::error_code status;
    const std::uintmax_t bytes = fs::file_size(m_path, status);

    if (status || bytes != m_bytes)
    {
        error = "input changed during extraction: " + m_path.string();
        return false;
    }

    const fs::file_time_type modified = fs::last_write_time(m_path, status);

    if (status || modified != m_modified)
    {
        error = "input changed during extraction: " + m_path.string();
        return false;
    }

    return true;
}

bool ResourceContainer::Read(const ResourceEntry & entry, std::vector<std::uint8_t> & output, std::string & error)
{
    if (!Unchanged(error))
    {
        return false;
    }

    output.resize(entry.length);
    m_stream.clear();
    m_stream.seekg(static_cast<std::streamoff>(entry.offset));

    if (!m_stream || (entry.length != 0 && !ReadExact(m_stream, output.data(), output.size())))
    {
        error = "short resource payload read: " + entry.name;
        return false;
    }

    return Unchanged(error);
}

const fs::path & ResourceContainer::Path() const
{
    return m_path;
}

const std::vector<ResourceEntry> & ResourceContainer::Entries() const
{
    return m_entries;
}

std::ifstream & ResourceContainer::Stream()
{
    return m_stream;
}

bool ResourceCatalog::Open(const std::vector<fs::path> & inputs, std::string & error)
{
    m_containers.clear();
    m_selected.clear();

    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
        auto container = std::make_unique<ResourceContainer>();

        if (!container->Open(inputs[index], index, error))
        {
            return false;
        }

        for (const auto & previous : m_containers)
        {
            if (previous->Path() == container->Path())
            {
                error = "same input container supplied twice";
                return false;
            }
        }

        for (const ResourceEntry & entry : container->Entries())
        {
            m_selected[entry.name] = entry;
        }

        m_containers.push_back(std::move(container));
    }

    return true;
}

const ResourceEntry * ResourceCatalog::Find(const std::string & name) const
{
    std::string normalized = name;

    if (!NormalizeResourceName(normalized))
    {
        return nullptr;
    }

    const auto found = m_selected.find(normalized);
    return found == m_selected.end() ? nullptr : &found->second;
}

bool ResourceCatalog::Read(const std::string & name, std::vector<std::uint8_t> & output, std::string & error)
{
    const ResourceEntry * entry = Find(name);

    if (entry == nullptr)
    {
        error = "resource not found: " + name;
        return false;
    }

    return m_containers[entry->container]->Read(*entry, output, error);
}

const std::map<std::string, ResourceEntry> & ResourceCatalog::Entries() const
{
    return m_selected;
}

} // namespace assets
