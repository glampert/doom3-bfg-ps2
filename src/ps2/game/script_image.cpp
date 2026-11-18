// ================================================================================================
// File: script_image.cpp
// Brief: Loads the cooked script program. The resident part of the file stays as read (defs,
// statements, lookups, strings, the initial globals), the build part becomes the engine's
// function and type objects, and debug builds also keep the source lines. Every index and range
// is checked here, at load, in every build: the interpreter trusts them afterwards.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

// Engine declarations depend on the platform and type setup in this header.
#include "idlib/precompiled.h"

#include "d3xp/Game_local.h"

#if defined(ID_SCRIPT_IMAGE)

#include "ps2/game/script_image.h"
#include "ps2/fnv.h"
#include "ps2/game/script_image_format.h"

#include <cstring>

namespace ps2::game::script
{
namespace
{

namespace format = image;

// ------------------------------------------------------------------------------------------------
// Reading
// ------------------------------------------------------------------------------------------------

// The file's parts, each in its own allocation.
struct Parts
{
    format::Header header;
    std::uint8_t * resident = nullptr;
    std::uint8_t * build = nullptr;
    std::uint8_t * debug = nullptr;

    ~Parts()
    {
        Mem_Free(build);
        Mem_Free(resident);
        Mem_Free(debug);
    }
};

PS2_COLD_FUNC void * AllocPart(std::uint32_t bytes, memTag_t tag)
{
    return Mem_Alloc16(static_cast<int>(bytes > 0 ? bytes : 4), tag);
}

PS2_COLD_FUNC bool ReadParts(const char * path, Parts & parts, idStr & error)
{
    idFile * const file = fileSystem->OpenFileRead(path);
    if (file == nullptr) [[unlikely]]
    {
        error = "the file is missing";
        return false;
    }

    format::Header & header = parts.header;
    const int length = file->Length();
    bool read = length >= static_cast<int>(sizeof(header)) && file->Read(&header, sizeof(header)) == sizeof(header);

    if (read)
    {
        const std::uint32_t debugBytes = header.fileBytes - header.residentBytes - header.buildBytes;
        if (header.magic != format::kMagic || header.version != format::kVersion) [[unlikely]]
        {
            error = "not a cooked script program of this version (cook it again)";
            read = false;
        }
        else if (header.fileBytes != static_cast<std::uint32_t>(length) || header.fileBytes > format::kMaxFileBytes ||
                 header.residentBytes < sizeof(header) || header.residentBytes > header.fileBytes ||
                 header.buildBytes > header.fileBytes - header.residentBytes ||
                 ((header.residentBytes | header.buildBytes | header.fileBytes) & 3) != 0) [[unlikely]]
        {
            error = "the part sizes do not match the file";
            read = false;
        }
        else
        {
            parts.resident = static_cast<std::uint8_t *>(AllocPart(header.residentBytes, TAG_SCRIPT));
            parts.build = static_cast<std::uint8_t *>(AllocPart(header.buildBytes, TAG_TEMP));
            std::memcpy(parts.resident, &header, sizeof(header));

            const int residentRest = static_cast<int>(header.residentBytes - sizeof(header));
            read = file->Read(parts.resident + sizeof(header), residentRest) == residentRest &&
                   file->Read(parts.build, static_cast<int>(header.buildBytes)) == static_cast<int>(header.buildBytes);

#if PS2_D3BFG_DEBUG
            // Source lines and file names serve diagnostics; release builds never read them.
            if (read)
            {
                parts.debug = static_cast<std::uint8_t *>(AllocPart(debugBytes, TAG_SCRIPT));
                read = file->Read(parts.debug, static_cast<int>(debugBytes)) == static_cast<int>(debugBytes);
            }
#else
            static_cast<void>(debugBytes);
#endif

            if (!read) [[unlikely]]
            {
                error = "the file ends early";
            }
        }
    }
    else
    {
        error = "the file is too short for its header";
    }

    fileSystem->CloseFile(file);
    if (!read)
    {
        return false;
    }

    std::uint32_t checksum = ps2::Fnv1a32(parts.resident + sizeof(header), header.residentBytes - sizeof(header));
    checksum = ps2::Fnv1a32(parts.build, header.buildBytes, checksum);
    if (checksum != header.residentChecksum) [[unlikely]]
    {
        error = "the program's checksum does not match its contents";
        return false;
    }

    if (parts.debug != nullptr &&
        ps2::Fnv1a32(parts.debug, header.fileBytes - header.residentBytes - header.buildBytes) != header.debugChecksum) [[unlikely]]
    {
        error = "the debug section's checksum does not match its contents";
        return false;
    }

    return true;
}

// ------------------------------------------------------------------------------------------------
// Section checks
// ------------------------------------------------------------------------------------------------

// A section of count records in one part: [partStart, partStart + partBytes) of the file.
template<typename T>
PS2_COLD_FUNC const T * Section(const std::uint8_t * part, std::uint32_t partStart, std::uint32_t partBytes, std::uint32_t offset,
                                std::uint32_t count, const char * name, idStr & error)
{
    const std::uint64_t bytes = static_cast<std::uint64_t>(count) * sizeof(T);
    if (offset < partStart || (offset & 3) != 0 || offset - partStart > partBytes || bytes > partBytes - (offset - partStart)) [[unlikely]]
    {
        error = va("the %s section lies outside its part", name);
        return nullptr;
    }

    return static_cast<const T *>(static_cast<const void *>(part + (offset - partStart)));
}

class Strings
{
public:
    Strings(const char * pool, std::uint32_t bytes) : m_pool(pool), m_bytes(bytes)
    {
    }

    bool Valid() const
    {
        return m_bytes > 0 && m_pool[m_bytes - 1] == '\0';
    }

    bool Has(std::uint32_t offset) const
    {
        return offset < m_bytes;
    }

    const char * Get(std::uint32_t offset) const
    {
        return m_pool + offset;
    }

private:
    const char * m_pool;
    std::uint32_t m_bytes;
};

// The jump a statement makes, as the interpreter reads it, or null.
PS2_COLD_FUNC const format::Def * JumpOperand(const format::Statement & statement, const format::Def * defs)
{
    switch (statement.op)
    {
        case OP_IF:
        case OP_IFNOT:
            return &defs[statement.b];

        case OP_GOTO:
            return &defs[statement.a];

        default:
            return nullptr;
    }
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Load
// ------------------------------------------------------------------------------------------------

bool ImageLoader::Load(idProgram & program, const char * path, idStr & error)
{
    Parts parts;
    if (!ReadParts(path, parts, error))
    {
        return false;
    }

    const format::Header & header = parts.header;
    const std::uint32_t buildStart = header.residentBytes;
    const std::uint32_t debugStart = header.residentBytes + header.buildBytes;
    const std::uint32_t debugBytes = header.fileBytes - debugStart;
    const std::uint8_t * const resident = parts.resident;

    // Resident sections.
    format::Def * const defs = const_cast<format::Def *>(Section<format::Def>(resident, 0, header.residentBytes, header.defsOffset, header.defCount, "defs", error));
    const format::Statement * const statements = Section<format::Statement>(resident, 0, header.residentBytes, header.statementsOffset, header.statementCount, "statements", error);
    const format::Lookup * const lookups = Section<format::Lookup>(resident, 0, header.residentBytes, header.lookupsOffset, header.lookupCount, "lookups", error);
    const format::Scope * const scopes = Section<format::Scope>(resident, 0, header.residentBytes, header.scopesOffset, header.scopeCount, "scopes", error);
    const char * const pool = Section<char>(resident, 0, header.residentBytes, header.stringsOffset, header.stringBytes, "strings", error);
    // function_t::parmSize reads them as int, which is the format's 32 bits on the EE.
    static_assert(sizeof(int) == sizeof(std::int32_t));
    const int * const parmSizes = Section<int>(resident, 0, header.residentBytes, header.parmSizesOffset, header.parmSizeCount, "argument sizes", error);
    const std::uint16_t * const typeRefs = Section<std::uint16_t>(resident, 0, header.residentBytes, header.typeRefsOffset, header.typeRefCount, "type references", error);
    const std::uint32_t * const fieldNames = Section<std::uint32_t>(resident, 0, header.residentBytes, header.fieldNamesOffset, header.typeRefCount, "field names", error);
    const std::uint16_t * const functionRefs = Section<std::uint16_t>(resident, 0, header.residentBytes, header.functionRefsOffset, header.functionRefCount, "virtual functions", error);
    const std::uint8_t * const globals = Section<std::uint8_t>(resident, 0, header.residentBytes, header.globalsOffset, header.globalBytes, "globals", error);

    // Build sections.
    const format::Function * const functionRecords = Section<format::Function>(parts.build, buildStart, header.buildBytes, header.functionsOffset, header.functionCount, "functions", error);
    const format::Type * const typeRecords = Section<format::Type>(parts.build, buildStart, header.buildBytes, header.typesOffset, header.typeCount, "types", error);
    const format::Event * const eventRecords = Section<format::Event>(parts.build, buildStart, header.buildBytes, header.eventsOffset, header.eventCount, "events", error);

    if (defs == nullptr || statements == nullptr || lookups == nullptr || scopes == nullptr || pool == nullptr || parmSizes == nullptr ||
        typeRefs == nullptr || fieldNames == nullptr || functionRefs == nullptr || globals == nullptr || functionRecords == nullptr ||
        typeRecords == nullptr || eventRecords == nullptr) [[unlikely]]
    {
        return false;
    }

    const Strings strings(pool, header.stringBytes);
    const std::uint32_t typeCount = format::kBuiltinTypes + header.typeCount;
    if (!strings.Valid() || header.defCount < 1 || header.defCount > format::kMaxIndex + 1 || header.statementCount < 1 ||
        typeCount > format::kMaxIndex || header.functionCount > format::kMaxIndex || header.eventCount > format::kMaxIndex) [[unlikely]]
    {
        error = "the program's counts are out of range";
        return false;
    }

    // --------------------------------------------------------------------------------------------
    // The engine's objects: functions, types and their tables in one allocation
    // --------------------------------------------------------------------------------------------

    const std::size_t functionsBytes = sizeof(function_t) * header.functionCount;
    const std::size_t typesBytes = sizeof(idTypeDef) * header.typeCount;
    const std::size_t tableBytes = sizeof(idTypeDef *) * typeCount;
    const std::size_t parmTypesBytes = sizeof(idTypeDef *) * header.typeRefCount;
    const std::size_t parmNamesBytes = sizeof(const char *) * header.typeRefCount;
    const std::size_t functionTablesBytes = sizeof(const function_t *) * header.functionRefCount;
    const std::size_t objectsBytes = functionsBytes + typesBytes + tableBytes + parmTypesBytes + parmNamesBytes + functionTablesBytes;

    std::uint8_t * const objects = static_cast<std::uint8_t *>(AllocPart(static_cast<std::uint32_t>(objectsBytes), TAG_SCRIPT));
    function_t * const functions = static_cast<function_t *>(static_cast<void *>(objects));
    idTypeDef * const types = static_cast<idTypeDef *>(static_cast<void *>(objects + functionsBytes));
    idTypeDef ** const typeTable = static_cast<idTypeDef **>(static_cast<void *>(objects + functionsBytes + typesBytes));
    idTypeDef ** const parmTypes = static_cast<idTypeDef **>(static_cast<void *>(objects + functionsBytes + typesBytes + tableBytes));
    const char ** const parmNames = static_cast<const char **>(static_cast<void *>(objects + functionsBytes + typesBytes + tableBytes + parmTypesBytes));
    const function_t ** const functionTables =
        static_cast<const function_t **>(static_cast<void *>(objects + functionsBytes + typesBytes + tableBytes + parmTypesBytes + parmNamesBytes));

    // From here the program owns every allocation, and FreeData releases them on failure.
    program.image = parts.resident;
    program.objects = objects;
    parts.resident = nullptr;

    for (std::uint32_t index = 0; index < format::kBuiltinTypes; ++index)
    {
        typeTable[index] = idVarDef::typeTable[index];
    }

    for (std::uint32_t index = 0; index < header.typeCount; ++index)
    {
        new (&types[index]) idTypeDef();
        typeTable[format::kBuiltinTypes + index] = &types[index];
    }

    for (std::uint32_t index = 0; index < header.functionCount; ++index)
    {
        new (&functions[index]) function_t();
    }

    // Types.
    for (std::uint32_t index = 0; index < header.typeCount; ++index)
    {
        const format::Type & record = typeRecords[index];
        idTypeDef & type = types[index];

        if (record.etype > ev_boolean || (record.aux != format::kNone && record.aux >= typeCount) || !strings.Has(record.name) ||
            (record.def != format::kNone && record.def >= header.defCount) || record.parmStart > header.typeRefCount ||
            record.parmCount > header.typeRefCount - record.parmStart || record.functionStart > header.functionRefCount ||
            record.functionCount > header.functionRefCount - record.functionStart) [[unlikely]]
        {
            error = va("type %u is malformed", index);
            return false;
        }

        type.type = static_cast<etype_t>(record.etype);
        type.name = strings.Get(record.name);
        type.size = static_cast<int>(record.size);
        type.auxType = record.aux != format::kNone ? typeTable[record.aux] : nullptr;
        type.def = record.def != format::kNone ? static_cast<idVarDef *>(static_cast<void *>(&defs[record.def])) : nullptr;
        type.numParms = record.parmCount;
        type.parmTypes = parmTypes + record.parmStart;
        type.parmNames = record.etype == ev_object ? parmNames + record.parmStart : nullptr;
        type.numFunctions = record.functionCount;
        type.functions = functionTables + record.functionStart;

        if (record.etype == ev_object && (record.def == format::kNone || record.aux == format::kNone)) [[unlikely]]
        {
            error = va("object type '%s' has no def or superclass", type.name);
            return false;
        }

        for (std::uint32_t parm = 0; parm < record.parmCount; ++parm)
        {
            const std::uint32_t slot = record.parmStart + parm;
            if (typeRefs[slot] >= typeCount || !strings.Has(fieldNames[slot])) [[unlikely]]
            {
                error = va("type '%s' has a malformed parameter", type.name);
                return false;
            }

            parmTypes[slot] = typeTable[typeRefs[slot]];
            parmNames[slot] = strings.Get(fieldNames[slot]);
        }

        for (std::uint32_t function = 0; function < record.functionCount; ++function)
        {
            const std::uint32_t slot = record.functionStart + function;
            if (functionRefs[slot] >= header.functionCount) [[unlikely]]
            {
                error = va("object type '%s' has a malformed virtual function", type.name);
                return false;
            }

            functionTables[slot] = &functions[functionRefs[slot]];
        }
    }

    // Object types end at the built-in object type; a cycle would hang Inherits.
    for (std::uint32_t index = 0; index < header.typeCount; ++index)
    {
        if (types[index].type != ev_object)
        {
            continue;
        }

        const idTypeDef * type = &types[index];
        std::uint32_t steps = 0;
        while (type != &type_object && type != nullptr && type->type == ev_object && steps <= header.typeCount)
        {
            type = type->auxType;
            ++steps;
        }

        if (type != &type_object) [[unlikely]]
        {
            error = va("object type '%s' does not derive from the object type", types[index].name);
            return false;
        }
    }

    // Events: the image's script events must be the game's, with the same arguments.
    for (std::uint32_t index = 0; index < header.eventCount; ++index)
    {
        const format::Event & record = eventRecords[index];
        if (!strings.Has(record.name) || !strings.Has(record.format)) [[unlikely]]
        {
            error = va("event %u is malformed", index);
            return false;
        }

        const idEventDef * const event = idEventDef::FindEvent(strings.Get(record.name));
        if (event == nullptr || idStr::Cmp(event->GetArgFormat(), strings.Get(record.format)) != 0 ||
            static_cast<std::uint8_t>(event->GetReturnType()) != record.returnType ||
            idThread::Type.RespondsTo(*event) != (record.thread != 0)) [[unlikely]]
        {
            error = va("event '%s' differs from the game's (regenerate src/tools/script_cook/events.txt and cook again)", strings.Get(record.name));
            return false;
        }
    }

    // Functions.
    for (std::uint32_t index = 0; index < header.functionCount; ++index)
    {
        const format::Function & record = functionRecords[index];
        function_t & function = functions[index];

        if (!strings.Has(record.name) || (record.event != format::kNone && record.event >= header.eventCount) ||
            (record.type != format::kNone && record.type >= typeCount) || record.firstStatement > header.statementCount ||
            record.numStatements > header.statementCount - record.firstStatement || record.parmTotal > record.locals ||
            record.locals > LOCALSTACK_SIZE || record.parmSizeStart > header.parmSizeCount ||
            record.parmSizeCount > header.parmSizeCount - record.parmSizeStart || record.file >= header.fileCount) [[unlikely]]
        {
            error = va("function %u is malformed", index);
            return false;
        }

        function.name = strings.Get(record.name);
        function.eventdef = record.event != format::kNone ? idEventDef::FindEvent(strings.Get(eventRecords[record.event].name)) : nullptr;
        function.type = record.type != format::kNone ? typeTable[record.type] : nullptr;
        function.firstStatement = static_cast<int>(record.firstStatement);
        function.numStatements = static_cast<int>(record.numStatements);
        function.parmTotal = static_cast<int>(record.parmTotal);
        function.locals = static_cast<int>(record.locals);
        function.filenum = record.file;
        function.parmSize.list = parmSizes + record.parmSizeStart;
        function.parmSize.num = record.parmSizeCount;
    }

    // --------------------------------------------------------------------------------------------
    // Defs, relocated in place into the engine's idVarDef
    // --------------------------------------------------------------------------------------------

    byte * const variables = static_cast<byte *>(AllocPart(header.globalBytes, TAG_SCRIPT));
    program.variables = variables;
    if (header.globalBytes > 0)
    {
        std::memcpy(variables, globals, header.globalBytes);
    }

    // The program's own defs, the globals calls return values through, checked before the defs
    // are relocated. The compiler retypes the return def to each call's return type, so its
    // storage, a vector, is checked rather than its type.
    const auto globalRoom = [&](std::uint16_t def, std::uint32_t bytes) {
        return def != 0 && def < header.defCount && defs[def].kind == static_cast<std::uint8_t>(format::ValueKind::Global) &&
               defs[def].value <= header.globalBytes && bytes <= header.globalBytes - defs[def].value;
    };

    if (!globalRoom(header.returnDef, static_cast<std::uint32_t>(type_vector.Size())) ||
        !globalRoom(header.returnStringDef, MAX_STRING_LEN) || typeTable[defs[header.returnStringDef].type] != &type_string) [[unlikely]]
    {
        error = "the return defs are malformed";
        return false;
    }

    if (defs[0].kind != static_cast<std::uint8_t>(format::ValueKind::None)) [[unlikely]]
    {
        error = "def 0 is not the placeholder";
        return false;
    }

    for (std::uint32_t index = 0; index < header.defCount; ++index)
    {
        format::Def & record = defs[index];
        if (record.storage > static_cast<std::uint8_t>(format::Storage::Stack) || record.type >= typeCount) [[unlikely]]
        {
            error = va("def %u is malformed", index);
            return false;
        }

        idVarDef & def = *static_cast<idVarDef *>(static_cast<void *>(&record));
        switch (static_cast<format::ValueKind>(record.kind))
        {
            case format::ValueKind::Int:
                break;

            case format::ValueKind::Global:
            {
                const idTypeDef * const type = typeTable[record.type];
                const std::uint32_t offset = record.value;
                bool fits = offset <= header.globalBytes;
                if (fits && type->Type() == ev_string && record.storage == static_cast<std::uint8_t>(format::Storage::Constant))
                {
                    // String constants are packed to their length: they must end inside the globals.
                    fits = std::memchr(variables + offset, '\0', header.globalBytes - offset) != nullptr;
                }
                else if (fits)
                {
                    fits = static_cast<std::uint32_t>(type->Size()) <= header.globalBytes - offset;
                }

                if (!fits) [[unlikely]]
                {
                    error = va("def %u lies outside the global variables", index);
                    return false;
                }

                def.value.bytePtr = variables + offset;
                break;
            }

            case format::ValueKind::Function:
                if (record.value >= header.functionCount) [[unlikely]]
                {
                    error = va("def %u names no function", index);
                    return false;
                }

                def.value.functionPtr = &functions[record.value];
                break;

            case format::ValueKind::None:
                def.value.intPtr = nullptr;
                break;

            default:
                error = va("def %u has an unknown value kind", index);
                return false;
        }
    }

    // Statements, and the jumps inside each function.
    for (std::uint32_t index = 0; index < header.statementCount; ++index)
    {
        const format::Statement & statement = statements[index];
        if (statement.op >= NUM_OPCODES || statement.a >= header.defCount || statement.b >= header.defCount || statement.c >= header.defCount) [[unlikely]]
        {
            error = va("statement %u is malformed", index);
            return false;
        }
    }

    for (std::uint32_t index = 0; index < header.functionCount; ++index)
    {
        const format::Function & record = functionRecords[index];
        const std::uint32_t end = record.firstStatement + record.numStatements;
        for (std::uint32_t at = record.firstStatement; at < end; ++at)
        {
            const format::Def * const jump = JumpOperand(statements[at], defs);
            if (jump == nullptr)
            {
                continue;
            }

            const std::int64_t target = static_cast<std::int64_t>(at) + static_cast<std::int32_t>(jump->value);
            if (jump->kind != static_cast<std::uint8_t>(format::ValueKind::Int) || target < record.firstStatement || target >= end) [[unlikely]]
            {
                error = va("a jump in '%s' leaves the function", functions[index].name);
                return false;
            }
        }
    }

    // Lookups (sorted, for GetDef's binary search) and scopes (sorted by def).
    for (std::uint32_t index = 0; index < header.lookupCount; ++index)
    {
        const format::Lookup & lookup = lookups[index];
        if (!strings.Has(lookup.name) || lookup.def == 0 || lookup.def >= header.defCount ||
            (lookup.scope != format::kNone && lookup.scope >= header.defCount) ||
            (index > 0 && idStr::Cmp(strings.Get(lookups[index - 1].name), strings.Get(lookup.name)) > 0)) [[unlikely]]
        {
            error = va("lookup %u is malformed or out of order", index);
            return false;
        }
    }

    for (std::uint32_t index = 0; index < header.scopeCount; ++index)
    {
        const format::Scope & scope = scopes[index];
        if (scope.def == 0 || scope.def >= header.defCount || (scope.parent != format::kNone && scope.parent >= header.defCount) ||
            (index > 0 && scopes[index - 1].def >= scope.def)) [[unlikely]]
        {
            error = va("scope %u is malformed or out of order", index);
            return false;
        }
    }

    // --------------------------------------------------------------------------------------------
    // Debug sections
    // --------------------------------------------------------------------------------------------

    if (parts.debug != nullptr)
    {
        const std::uint16_t * const lines = Section<std::uint16_t>(parts.debug, debugStart, debugBytes, header.linesOffset, header.statementCount, "lines", error);
        const format::FileRun * const runs = Section<format::FileRun>(parts.debug, debugStart, debugBytes, header.fileRunsOffset, header.fileRunCount, "file runs", error);
        const std::uint32_t * const names = Section<std::uint32_t>(parts.debug, debugStart, debugBytes, header.fileNamesOffset, header.fileCount, "file names", error);
        const char * const debugPool = Section<char>(parts.debug, debugStart, debugBytes, header.debugStringsOffset, header.debugStringBytes, "debug strings", error);
        if (lines == nullptr || runs == nullptr || names == nullptr || debugPool == nullptr) [[unlikely]]
        {
            return false;
        }

        const Strings debugStrings(debugPool, header.debugStringBytes);
        bool valid = debugStrings.Valid() && header.fileRunCount > 0 && runs[0].firstStatement == 0;
        for (std::uint32_t index = 0; valid && index < header.fileRunCount; ++index)
        {
            valid = runs[index].file < header.fileCount && runs[index].firstStatement < header.statementCount &&
                    (index == 0 || runs[index - 1].firstStatement < runs[index].firstStatement);
        }

        for (std::uint32_t index = 0; valid && index < header.fileCount; ++index)
        {
            valid = debugStrings.Has(names[index]);
        }

        if (!valid) [[unlikely]]
        {
            error = "the debug section is malformed";
            return false;
        }

        // FreeData releases the file name table with the section.
        const char ** const fileNames = static_cast<const char **>(Mem_Alloc16(static_cast<int>(sizeof(const char *) * (header.fileCount + 1)), TAG_SCRIPT));
        for (std::uint32_t index = 0; index < header.fileCount; ++index)
        {
            fileNames[index] = debugStrings.Get(names[index]);
        }

        program.debugInfo = parts.debug;
        parts.debug = nullptr;
        program.lines = lines;
        program.fileRuns = runs;
        program.numFileRuns = static_cast<int>(header.fileRunCount);
        program.fileNames = fileNames;
        program.numFiles = static_cast<int>(header.fileCount);
    }

    // --------------------------------------------------------------------------------------------
    // Publish
    // --------------------------------------------------------------------------------------------

    program.statements = static_cast<statement_t *>(static_cast<void *>(const_cast<format::Statement *>(statements)));
    program.numStatements = static_cast<int>(header.statementCount);
    program.defs = static_cast<idVarDef *>(static_cast<void *>(defs));
    program.numDefs = static_cast<int>(header.defCount);
    program.lookups = lookups;
    program.numLookups = static_cast<int>(header.lookupCount);
    program.scopes = scopes;
    program.numScopes = static_cast<int>(header.scopeCount);
    program.strings = pool;
    program.functions = functions;
    program.numFunctions = static_cast<int>(header.functionCount);
    program.typeTable = typeTable;
    program.numTypes = static_cast<int>(typeCount);
    program.variableDefaults = globals;
    program.numVariables = static_cast<int>(header.globalBytes);
    program.checksum = static_cast<int>(header.residentChecksum);
    program.returnDef = &program.defs[header.returnDef];
    program.returnStringDef = &program.defs[header.returnStringDef];

    scriptOperand_t::defs = program.defs;
    idVarDef::typeTable = typeTable;
    return true;
}

} // namespace ps2::game::script

#elif defined(ID_SCRIPT_CHECK)

#include "ps2/game/script_image.h"
#include "ps2/fnv.h"
#include "ps2/game/script_image_format.h"
#include "ps2/system/log.h"
#include "tools/script_cook/script_image_writer.h"

#include <string>
#include <vector>

namespace ps2::game::script
{
namespace
{

bool ThreadResponds(const idEventDef & event)
{
    return idThread::Type.RespondsTo(event);
}

} // namespace

void ReportCompiledImage(const idProgram & program)
{
    std::vector<std::uint8_t> bytes;
    cook::ImageReport report;
    std::string error;
    if (!cook::ImageWriter::Write(program, ThreadResponds, bytes, report, error)) [[unlikely]]
    {
        FatalError("script image check: %s", error.c_str());
    }

    const std::uint32_t checksum = ps2::Fnv1a32(bytes.data(), bytes.size());
    Log(LogLevel::Info, "[D3BFG] SCRIPT_IMAGE bytes=%u fnv=%08x defs=%u statements=%u functions=%u types=%u globals=%u\n",
        static_cast<unsigned int>(bytes.size()), static_cast<unsigned int>(checksum), static_cast<unsigned int>(report.defs),
        static_cast<unsigned int>(report.statements), static_cast<unsigned int>(report.functions), static_cast<unsigned int>(report.types),
        static_cast<unsigned int>(report.globalBytes));
}

} // namespace ps2::game::script

#endif // defined(ID_SCRIPT_IMAGE), defined(ID_SCRIPT_CHECK)
