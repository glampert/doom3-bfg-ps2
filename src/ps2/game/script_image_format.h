// ================================================================================================
// File: script_image_format.h
// Brief: The cooked script program (.d3sp): the retail game script compiled offline by the host
// cook (src/tools/script_cook), in records the PS2 uses in place, so the console neither runs the
// compiler nor keeps its 4.9 MB of compile-time state.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include <cstdint>

namespace ps2::game::script::image
{

// ------------------------------------------------------------------------------------------------
// Layout
//
// Little endian; every section starts on a 4-byte boundary. The file is three parts:
//   resident  [0, residentBytes): the header and the sections the PS2 reads in place
//   build     [residentBytes, residentBytes + buildBytes): read into a temporary buffer and
//             turned into the engine's function and type objects
//   debug     [residentBytes + buildBytes, fileBytes): source lines and file names, which only
//             debug builds read
// Indices are 16 bits. Def 0 is a placeholder, so a statement operand of 0 means none. Types
// 0 to kBuiltinTypes - 1 are the engine's built-in types (type_void ... type_boolean, in etype
// order); the image's own types follow.
// ------------------------------------------------------------------------------------------------

static constexpr std::uint32_t kMagic = 0x50533344; // "D3SP"
static constexpr std::uint32_t kVersion = 1;
static constexpr std::uint32_t kBuiltinTypes = 15;
static constexpr std::uint16_t kNone = 0xFFFF;
static constexpr std::uint32_t kMaxIndex = 0xFFFE;
static constexpr std::uint32_t kMaxFileBytes = 8U * 1024U * 1024U;

// How a def's value field reads.
enum class ValueKind : std::uint8_t
{
    Int = 0,      // an integer kept verbatim: stack offset, jump, argument size, virtual function, field offset
    Global = 1,   // a byte offset into the global variables
    Function = 2, // a function index
    None = 3      // no value (namespaces, object type defs, the placeholder)
};

// idVarDef::initialized_t.
enum class Storage : std::uint8_t
{
    Uninitialized = 0,
    Variable = 1,
    Constant = 2,
    Stack = 3
};

struct Header
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t fileBytes;
    std::uint32_t residentBytes;
    std::uint32_t buildBytes;
    std::uint32_t residentChecksum; // ps2::Fnv1a32 of the resident and build parts after the header
    std::uint32_t debugChecksum;    // ps2::Fnv1a32 of the debug part

    // Resident sections: offsets from the start of the file.
    std::uint32_t defsOffset;
    std::uint32_t defCount;
    std::uint32_t statementsOffset;
    std::uint32_t statementCount;
    std::uint32_t lookupsOffset;
    std::uint32_t lookupCount;
    std::uint32_t scopesOffset;
    std::uint32_t scopeCount;
    std::uint32_t stringsOffset;
    std::uint32_t stringBytes;
    std::uint32_t parmSizesOffset; // int32 per function argument
    std::uint32_t parmSizeCount;
    std::uint32_t typeRefsOffset; // uint16 type index per parameter or field
    std::uint32_t typeRefCount;
    std::uint32_t fieldNamesOffset; // uint32 string offset per typeRef (0 for function parameters)
    std::uint32_t functionRefsOffset; // uint16 function index per virtual function
    std::uint32_t functionRefCount;
    std::uint32_t globalsOffset; // the globals' initial bytes, also what Restart restores
    std::uint32_t globalBytes;

    // Build sections.
    std::uint32_t functionsOffset;
    std::uint32_t functionCount;
    std::uint32_t typesOffset;
    std::uint32_t typeCount; // the image's own types, after the built-in ones
    std::uint32_t eventsOffset;
    std::uint32_t eventCount;

    // Debug sections.
    std::uint32_t linesOffset; // uint16 source line per statement
    std::uint32_t fileRunsOffset;
    std::uint32_t fileRunCount;
    std::uint32_t fileNamesOffset; // uint32 debug string offset per source file
    std::uint32_t fileCount;
    std::uint32_t debugStringsOffset;
    std::uint32_t debugStringBytes;

    // The program's own defs.
    std::uint16_t returnDef;
    std::uint16_t returnStringDef;
    std::uint16_t sysDef;
    std::uint16_t reserved;
};

// The PS2's idVarDef record exactly: the loader turns value into a pointer in place.
struct Def
{
    std::uint32_t value;
    std::uint8_t storage; // Storage
    std::uint8_t kind;    // ValueKind
    std::uint16_t type;
};

// The PS2's statement_t exactly.
struct Statement
{
    std::uint16_t op;
    std::uint16_t a;
    std::uint16_t b;
    std::uint16_t c;
};

// A def that name lookups can reach (namespace and object scope), sorted by name with strcmp.
struct Lookup
{
    std::uint32_t name;
    std::uint16_t def;
    std::uint16_t scope; // the scope's def, or kNone for the global namespace
};

// A namespace or object type def: the scopes GetDef walks out of, sorted by def.
struct Scope
{
    std::uint16_t def;
    std::uint16_t parent; // kNone for the global namespace
};

struct Function
{
    std::uint32_t name; // the global name, "namespace::function"
    std::uint16_t event; // kNone unless the function is a script event
    std::uint16_t type;
    std::uint32_t firstStatement;
    std::uint32_t numStatements;
    std::uint32_t parmTotal;
    std::uint32_t locals;
    std::uint16_t file;
    std::uint16_t parmSizeCount;
    std::uint32_t parmSizeStart;
};

struct Type
{
    std::uint8_t etype;
    std::uint8_t reserved;
    std::uint16_t aux; // the superclass, return, field or pointed-to type, or kNone
    std::uint32_t name; // string offset; object types are found by name
    std::uint32_t size;
    std::uint16_t def; // the type's own def (object types), or kNone
    std::uint16_t parmCount;
    std::uint32_t parmStart; // into typeRefs and fieldNames
    std::uint16_t functionCount;
    std::uint16_t reserved2;
    std::uint32_t functionStart; // into functionRefs
};

// A script event; the PS2 resolves it against its own registry and checks it matches.
struct Event
{
    std::uint32_t name;
    std::uint32_t format;
    std::uint8_t returnType;
    std::uint8_t thread; // 1 when idThread responds to it (a sys call)
    std::uint16_t reserved;
};

// The source file of the statements from firstStatement to the next run.
struct FileRun
{
    std::uint32_t firstStatement;
    std::uint32_t file;
};

static_assert(sizeof(Header) == 164);
static_assert(sizeof(Def) == 8);
static_assert(sizeof(Statement) == 8);
static_assert(sizeof(Lookup) == 8);
static_assert(sizeof(Scope) == 4);
static_assert(sizeof(Function) == 32);
static_assert(sizeof(Type) == 28);
static_assert(sizeof(Event) == 12);
static_assert(sizeof(FileRun) == 8);

} // namespace ps2::game::script::image
