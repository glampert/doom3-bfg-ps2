// ================================================================================================
// File: resources.h
// Brief: Validate native Doom 3 BFG .resources containers and read their records, so every host
// tool resolves retail files exactly as the engine's lookup does.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace assets
{

struct ResourceEntry
{
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    std::size_t container = 0;
};

// Lowercases ASCII, converts backslashes and collapses repeated separators (the alias the
// filesystem would apply anyway). Refuses empty, absolute, dot, control-character and Windows
// device-name components, so a record can never address a path outside its output root.
bool NormalizeResourceName(std::string & name);

// One container: big-endian header and table, little-endian name lengths, payloads stored
// directly. Open validates every bound and refuses partially overlapping payloads.
class ResourceContainer final
{
public:
    // Errors describe the input; callers add their own prefix.
    bool Open(const std::filesystem::path & input, std::size_t index, std::string & error);
    // True while the file keeps the size and timestamp it had when opened.
    bool Unchanged(std::string & error) const;
    bool Read(const ResourceEntry & entry, std::vector<std::uint8_t> & output, std::string & error);
    const std::filesystem::path & Path() const;
    const std::vector<ResourceEntry> & Entries() const;
    std::ifstream & Stream();

private:
    std::filesystem::path m_path;
    std::ifstream m_stream;
    std::uintmax_t m_bytes = 0;
    std::filesystem::file_time_type m_modified{};
    std::vector<ResourceEntry> m_entries;
};

// Several containers merged by name: later containers and later records win, the engine's
// resource lookup order for _common then _ordered.
class ResourceCatalog final
{
public:
    bool Open(const std::vector<std::filesystem::path> & inputs, std::string & error);
    const ResourceEntry * Find(const std::string & name) const;
    bool Read(const std::string & name, std::vector<std::uint8_t> & output, std::string & error);
    const std::map<std::string, ResourceEntry> & Entries() const;

private:
    std::vector<std::unique_ptr<ResourceContainer> > m_containers;
    std::map<std::string, ResourceEntry> m_selected;
};

} // namespace assets
