// ================================================================================================
// File: filesystem_tests.cpp
// Brief: Exercise real stdio, directory filters, bounded paths and UTC ZIP encoding under sanitizers.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/filesystem.h"
#include "ps2/system/log.h"

#include <cerrno>
#include <climits>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace
{

bool Check(const char * name, bool passed)
{
    ps2::Log(ps2::LogLevel::Info, "filesystem/%s %s\n", name, passed ? "PASS" : "FAIL");
    return passed;
}

struct Listing
{
    int count = 0;
    bool text = false;
    bool folder = false;
};

void Visit(const char * name, void * context)
{
    Listing & listing = *static_cast<Listing *>(context);
    ++listing.count;
    listing.text = listing.text || std::strcmp(name, "mixed.TxT") == 0;
    listing.folder = listing.folder || std::strcmp(name, "child") == 0;
}

bool Paths()
{
    namespace fs = ps2::filesystem;
    char path[fs::kPathCapacity] = {};
    bool passed = fs::IsRelativePath("models/a..b.md5mesh") && fs::IsRelativePath("", true) &&
        fs::IsValidPath("host:/base/") && fs::IsValidPath("pfs0:base/file") && fs::IsOSPath("host:");
    const char * badPaths[] = {"../x", "a/../x", "./x", "a//b", "a/", "/x", "mass:x", "a\\b", "a\nb"};
    for (const char * bad : badPaths)
    { passed = !fs::IsRelativePath(bad) && passed; }
    passed = !fs::IsValidPath("host:/../x") && !fs::IsValidPath("host::x") && !fs::IsValidPath("a//b") && passed;
    passed = fs::BuildPath(path, sizeof(path), "host:", "base", "maps/test.map") &&
        std::strcmp(path, "host:/base/maps/test.map") == 0 && passed;
    passed = fs::BuildPath(path, sizeof(path), path, "", "child") &&
        std::strcmp(path, "host:/base/maps/test.map/child") == 0 && passed;
    passed = fs::BuildPath(path, sizeof(path), "/tmp/", "", "") && std::strcmp(path, "/tmp/") == 0 && passed;
    char shortPath[4] = "old";
    passed = !fs::BuildPath(shortPath, sizeof(shortPath), "host:", "base", "x") && shortPath[0] == '\0' && passed;
    char longPath[fs::kPathCapacity + 1];
    std::memset(longPath, 'a', sizeof(longPath) - 1);
    longPath[sizeof(longPath) - 1] = '\0';
    passed = !fs::IsRelativePath(longPath) && !fs::BuildPath(path, sizeof(path), "host:", "base", longPath) && passed;
    return Check("paths", passed);
}

bool Files()
{
    namespace fs = ps2::filesystem;
    bool passed = fs::CreateParents("work/child/mixed.TxT");
    FILE * file = fs::Open("work/mixed.TxT", fs::OpenMode::Write);
    if (file == nullptr) { return Check("stdio", false); }
    passed = std::fwrite("abcdef", 1, 6, file) == 6 && std::fclose(file) == 0 && passed;
    file = fs::Open("work/mixed.TxT", fs::OpenMode::Append);
    if (file == nullptr) { return Check("stdio", false); }
    passed = std::ftell(file) == 6 && std::fwrite("gh", 1, 2, file) == 2 && std::fclose(file) == 0 && passed;
    file = fs::Open("work/mixed.TxT", fs::OpenMode::Read);
    if (file == nullptr) { return Check("stdio", false); }
    char data[9] = {};
    passed = std::fseek(file, 3, SEEK_SET) == 0 && fs::FileLength(file) == 8 && std::ftell(file) == 3 && passed;
    passed = fs::ReadExact(file, data, 5) && std::strcmp(data, "defgh") == 0 && std::fread(data, 1, 1, file) == 0 && passed;
    passed = std::fseek(file, -2, SEEK_END) == 0 && !fs::ReadExact(file, data, 3) && std::ftell(file) == 8 && passed;
    passed = std::fclose(file) == 0 && fs::Open("missing", fs::OpenMode::Read) == nullptr && passed;
    // Verify range rejection against a real sparse file; FileLength must still restore its cursor.
    file = fs::Open("work/large", fs::OpenMode::Write);
    if (file == nullptr) { return Check("stdio", false); }
    passed = ftruncate(fileno(file), static_cast<off_t>(INT_MAX) + 1) == 0 &&
        std::fseek(file, 7, SEEK_SET) == 0 && fs::FileLength(file) == -1 && errno == EOVERFLOW && std::ftell(file) == 7 && passed;
    passed = std::fclose(file) == 0 && fs::Remove("work/large") && passed;
    return Check("stdio", passed);
}

bool Directories()
{
    namespace fs = ps2::filesystem;
    // Symlinks must not masquerade as regular files or directory recursion candidates.
    bool passed = symlink("mixed.TxT", "work/link.txt") == 0 && symlink("child", "work/link-dir") == 0;
    Listing files;
    passed = fs::List("work", ".txt", Visit, &files) == 1 && files.count == 1 && files.text && passed;
    Listing folders;
    passed = fs::List("work", "/", Visit, &folders) == 1 && folders.count == 1 && folders.folder && passed;
    Listing all;
    passed = fs::List("work", nullptr, Visit, &all) == 1 && all.text && passed;
    Listing missing;
    passed = fs::List("missing", "", Visit, &missing) == -1 && missing.count == 0 && passed;
    passed = !fs::CreateParents("work/mixed.TxT/file") &&
        fs::Rename("work/mixed.TxT", "work/renamed.txt") && fs::Remove("work/renamed.txt") &&
        !fs::Rename("work/absent", "work/renamed.txt") && !fs::Remove("work/absent") && passed;
    return Check("directories-errors", passed);
}

bool Times()
{
    namespace fs = ps2::filesystem;
    tm date = {};
    date.tm_year = 126; date.tm_mon = 9; date.tm_mday = 9;
    date.tm_hour = 13; date.tm_min = 14; date.tm_sec = 15;
    bool passed = fs::PackZipTime(date) == (46u << 25 | 10u << 21 | 9u << 16 | 13u << 11 | 14u << 5 | 7u);
    std::uint32_t packed = 0;
    passed = fs::ZipTime("timestamp", packed) && packed == fs::PackZipTime(date) && passed;
    date.tm_year = 70;
    passed = fs::PackZipTime(date) == (1u << 21 | 1u << 16) && passed;
    date.tm_year = 208;
    passed = fs::PackZipTime(date) == (127u << 25 | 12u << 21 | 31u << 16 | 23u << 11 | 59u << 5 | 29u) && passed;
    packed = 1;
    passed = !fs::ZipTime("missing", packed) && packed == 0 && passed;
    return Check("zip-utc", passed);
}

} // namespace

int main()
{
    bool passed = Paths();
    passed = Files() && passed;
    passed = Directories() && passed;
    passed = Times() && passed;
    return passed ? 0 : 1;
}
