// ================================================================================================
// File: script_image_writer.cpp
// Brief: Serializes a compiled program into the cooked image. It keeps what runs (statements, the
// defs they name, functions, types, the globals) and the defs the game looks up by name, drops
// the compiler's locals, temporaries and their names, packs string constants to their length and
// shares one type among functions of the same signature.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

// The standard headers come first: idlib's precompiled.h redefines printf-family names.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "idlib/precompiled.h"

#if defined(ID_SCRIPT_COOK)
#include "tools/script_cook/cook_game.h"
#else
#include "d3xp/Game_local.h"
#endif

#include "ps2/fnv.h"
#include "ps2/game/script_image_format.h"
#include "tools/script_cook/script_image_writer.h"

namespace format = ps2::game::script::image;

namespace
{

// ------------------------------------------------------------------------------------------------
// Byte output
// ------------------------------------------------------------------------------------------------

class Output
{
public:
    std::uint32_t Size() const
    {
        return static_cast<std::uint32_t>(m_bytes.size());
    }

    void Align()
    {
        while ((m_bytes.size() & 3) != 0)
        {
            m_bytes.push_back(0);
        }
    }

    std::uint32_t Append(const void * data, std::size_t bytes)
    {
        Align();
        const std::uint32_t offset = Size();
        const std::uint8_t * const cursor = static_cast<const std::uint8_t *>(data);
        m_bytes.insert(m_bytes.end(), cursor, cursor + bytes);
        return offset;
    }

    template<typename T>
    std::uint32_t AppendArray(const std::vector<T> & items)
    {
        return Append(items.data(), items.size() * sizeof(T));
    }

    std::vector<std::uint8_t> & Bytes()
    {
        return m_bytes;
    }

private:
    std::vector<std::uint8_t> m_bytes;
};

// Each distinct string once, offset 0 being the empty string.
class StringPool
{
public:
    StringPool()
    {
        m_bytes.push_back('\0');
        m_offsets[""] = 0;
    }

    std::uint32_t Add(const char * text)
    {
        const std::string key = text != nullptr ? text : "";
        const auto found = m_offsets.find(key);
        if (found != m_offsets.end())
        {
            return found->second;
        }

        const std::uint32_t offset = static_cast<std::uint32_t>(m_bytes.size());
        m_bytes.insert(m_bytes.end(), key.begin(), key.end());
        m_bytes.push_back('\0');
        m_offsets[key] = offset;
        return offset;
    }

    const std::vector<char> & Bytes() const
    {
        return m_bytes;
    }

private:
    std::vector<char> m_bytes;
    std::map<std::string, std::uint32_t> m_offsets;
};

bool IsStringConstant(const idVarDef * def)
{
    return def->Type() == ev_string && def->initialized == idVarDef::initializedConstant &&
           idStr::Cmpn(def->Name(), "<IMMEDIATE>", 11) == 0;
}

// The length of a string constant's text, which never exceeds its MAX_STRING_LEN slot.
std::size_t ConstantLength(const char * text)
{
    std::size_t length = 0;
    while (length < MAX_STRING_LEN - 1 && text[length] != '\0')
    {
        ++length;
    }

    return length;
}

// A def that holds its value itself rather than in the globals.
bool KeepsValueInDef(etype_t type)
{
    return type == ev_jumpoffset || type == ev_argsize || type == ev_virtualfunction;
}

} // namespace

namespace cook
{

bool ImageWriter::Write(const idProgram & program, bool (*threadResponds)(const idEventDef & event), std::vector<std::uint8_t> & image,
                        ImageReport & report, std::string & error)
{
    // --------------------------------------------------------------------------------------------
    // Types: the built-in ones by etype, then the program's, one per distinct function signature
    // --------------------------------------------------------------------------------------------

    const idTypeDef * const builtins[format::kBuiltinTypes] = {
        &type_void, &type_scriptevent, &type_namespace, &type_string, &type_float,
        &type_vector, &type_entity, &type_field, &type_function, &type_virtualfunction,
        &type_pointer, &type_object, &type_jumpoffset, &type_argsize, &type_boolean};

    std::map<const idTypeDef *, std::uint32_t> typeIndex;
    for (std::uint32_t index = 0; index < format::kBuiltinTypes; ++index)
    {
        if (builtins[index]->Type() != static_cast<etype_t>(index))
        {
            error = "the built-in script types are not in etype order";
            return false;
        }

        typeIndex[builtins[index]] = index;
    }

    std::vector<const idTypeDef *> emittedTypes;
    for (int index = 0; index < program.types.Num(); ++index)
    {
        const idTypeDef * const type = program.types[index];
        if (type->Type() != ev_function)
        {
            typeIndex[type] = format::kBuiltinTypes + static_cast<std::uint32_t>(emittedTypes.size());
            emittedTypes.push_back(type);
        }
    }

    const auto findType = [&](const idTypeDef * type, std::uint16_t & index) -> bool {
        const auto found = typeIndex.find(type);
        if (found == typeIndex.end())
        {
            error = std::string("a reference to a type the program does not list: ") + (type != nullptr ? type->Name() : "(null)");
            return false;
        }

        index = static_cast<std::uint16_t>(found->second);
        return true;
    };

    // Function types differ only by name (each function gets its own); the interpreter and the
    // game read only their parameter and return types.
    std::map<std::vector<std::uint16_t>, std::uint32_t> signatures;
    for (int index = 0; index < program.types.Num(); ++index)
    {
        const idTypeDef * const type = program.types[index];
        if (type->Type() != ev_function)
        {
            continue;
        }

        std::vector<std::uint16_t> key;
        std::uint16_t reference = 0;
        if (!findType(type->auxType, reference))
        {
            return false;
        }

        key.push_back(reference);
        for (int parm = 0; parm < type->NumParameters(); ++parm)
        {
            if (type->GetParmType(parm)->Type() == ev_function || !findType(type->GetParmType(parm), reference))
            {
                error = "a function type with a function parameter";
                return false;
            }

            key.push_back(reference);
        }

        const auto found = signatures.find(key);
        if (found != signatures.end())
        {
            typeIndex[type] = found->second;
            continue;
        }

        const std::uint32_t newIndex = format::kBuiltinTypes + static_cast<std::uint32_t>(emittedTypes.size());
        signatures[key] = newIndex;
        typeIndex[type] = newIndex;
        emittedTypes.push_back(type);
    }

    if (format::kBuiltinTypes + emittedTypes.size() > format::kMaxIndex)
    {
        error = "too many script types for 16-bit indices";
        return false;
    }

    // --------------------------------------------------------------------------------------------
    // Defs: those statements name, those lookups reach, and the program's own
    // --------------------------------------------------------------------------------------------

    const int defCount = program.varDefs.Num();
    std::vector<bool> needed(static_cast<std::size_t>(defCount), false);
    std::vector<bool> lookup(static_cast<std::size_t>(defCount), false);
    std::vector<bool> scope(static_cast<std::size_t>(defCount), false);

    const auto checkDef = [&](const idVarDef * def) -> bool {
        if (def == nullptr || def->num < 0 || def->num >= defCount || program.varDefs[def->num] != def)
        {
            error = "a reference to a def the program does not list";
            return false;
        }

        return true;
    };

    for (int index = 0; index < program.statements.Num(); ++index)
    {
        const statement_t & statement = program.statements[index];
        const idVarDef * const operands[3] = {statement.a, statement.b, statement.c};
        for (const idVarDef * operand : operands)
        {
            if (operand == nullptr)
            {
                continue;
            }

            if (!checkDef(operand))
            {
                return false;
            }

            needed[static_cast<std::size_t>(operand->num)] = true;
        }
    }

    // Name lookups (FindFunction, FindType's object functions, SetEntity, GetDef) start from a
    // namespace or an object, never a function's locals; immediates and results are unnamed.
    for (int index = 0; index < defCount; ++index)
    {
        const idVarDef * const def = program.varDefs[index];
        if (def->scope == nullptr || def->scope->Type() == ev_function || def->Name()[0] == '<')
        {
            continue;
        }

        lookup[static_cast<std::size_t>(index)] = true;
        needed[static_cast<std::size_t>(index)] = true;
        for (const idVarDef * outer = def->scope; outer != &def_namespace; outer = outer->scope)
        {
            if (outer == nullptr || !checkDef(outer))
            {
                error = "a def whose scope is outside the program";
                return false;
            }

            scope[static_cast<std::size_t>(outer->num)] = true;
            needed[static_cast<std::size_t>(outer->num)] = true;
        }
    }

    for (const idTypeDef * type : emittedTypes)
    {
        if (type->Type() == ev_object)
        {
            if (!checkDef(type->def))
            {
                return false;
            }

            scope[static_cast<std::size_t>(type->def->num)] = true;
            needed[static_cast<std::size_t>(type->def->num)] = true;
        }
    }

    const idVarDef * const programDefs[3] = {program.returnDef, program.returnStringDef, program.sysDef};
    for (const idVarDef * def : programDefs)
    {
        if (!checkDef(def))
        {
            return false;
        }

        needed[static_cast<std::size_t>(def->num)] = true;
    }

    // Scopes walk out to the global namespace through other scopes.
    for (int index = 0; index < defCount; ++index)
    {
        if (!scope[static_cast<std::size_t>(index)])
        {
            continue;
        }

        for (const idVarDef * outer = program.varDefs[index]->scope; outer != &def_namespace; outer = outer->scope)
        {
            if (outer == nullptr || !checkDef(outer))
            {
                error = "a scope outside the program";
                return false;
            }

            scope[static_cast<std::size_t>(outer->num)] = true;
            needed[static_cast<std::size_t>(outer->num)] = true;
        }
    }

    std::vector<std::uint16_t> defIndex(static_cast<std::size_t>(defCount), format::kNone);
    std::vector<const idVarDef *> emittedDefs(1, nullptr);
    for (int index = 0; index < defCount; ++index)
    {
        if (needed[static_cast<std::size_t>(index)])
        {
            if (emittedDefs.size() > format::kMaxIndex)
            {
                error = "too many script defs for 16-bit indices";
                return false;
            }

            defIndex[static_cast<std::size_t>(index)] = static_cast<std::uint16_t>(emittedDefs.size());
            emittedDefs.push_back(program.varDefs[index]);
        }
    }

    const auto indexOf = [&](const idVarDef * def) -> std::uint16_t {
        return def == nullptr ? 0 : defIndex[static_cast<std::size_t>(def->num)];
    };

    // --------------------------------------------------------------------------------------------
    // Globals: the bytes the emitted defs own, string constants packed to their length
    // --------------------------------------------------------------------------------------------

    const byte * const globals = &program.variables[0];
    const std::uint32_t compiledBytes = static_cast<std::uint32_t>(program.numVariables);

    struct Region
    {
        std::uint32_t offset;
        std::uint32_t size;
        std::uint32_t packedSize;
        std::uint32_t newOffset;
    };

    // How each emitted def's value reads; globals are resolved once the regions are laid out.
    std::vector<format::ValueKind> kinds(emittedDefs.size(), format::ValueKind::None);
    std::vector<std::uint32_t> values(emittedDefs.size(), 0);
    std::vector<Region> regions;

    std::map<const function_t *, std::uint32_t> functionIndex;
    for (int index = 0; index < program.functions.Num(); ++index)
    {
        functionIndex[&program.functions[index]] = static_cast<std::uint32_t>(index);
    }

    for (std::size_t index = 1; index < emittedDefs.size(); ++index)
    {
        const idVarDef * const def = emittedDefs[index];
        const etype_t type = def->Type();
        const bool objectScope = def->scope != nullptr && def->scope->TypeDef()->Inherits(&type_object);

        // The compiler retypes the program's return def to each call's return type, so its final
        // type says nothing of its storage: a vector (BeginCompilation), whatever is returned.
        const bool returnDef = def == program.returnDef;

        if (!returnDef && (def->initialized == idVarDef::stackVariable || KeepsValueInDef(type) || (objectScope && type != ev_function)))
        {
            kinds[index] = format::ValueKind::Int;
            values[index] = static_cast<std::uint32_t>(def->value.ptrOffset);
        }
        else if (!returnDef && type == ev_function)
        {
            if (def->value.functionPtr == nullptr)
            {
                continue;
            }

            const auto found = functionIndex.find(def->value.functionPtr);
            if (found == functionIndex.end())
            {
                error = std::string("function def '") + def->Name() + "' names a function outside the program";
                return false;
            }

            kinds[index] = format::ValueKind::Function;
            values[index] = found->second;
        }
        else if (!returnDef && (type == ev_namespace || type == ev_void || (type == ev_object && def->TypeDef()->def == def)))
        {
            // Namespaces, the sys object and object types' own defs: only their names matter.
            continue;
        }
        else
        {
            const std::ptrdiff_t offset = def->value.bytePtr - globals;
            const std::uint32_t size = static_cast<std::uint32_t>(returnDef ? type_vector.Size() : def->TypeDef()->Size());
            if (offset < 0 || static_cast<std::uint32_t>(offset) + size > compiledBytes)
            {
                error = std::string("global '") + def->Name() + "' lies outside the global variables";
                return false;
            }

            std::uint32_t packedSize = size;
            if (IsStringConstant(def))
            {
                const char * const text = reinterpret_cast<const char *>(globals + offset);
                const std::size_t length = ConstantLength(text);
                packedSize = static_cast<std::uint32_t>((length + 1 + 3) & ~static_cast<std::size_t>(3));
            }

            kinds[index] = format::ValueKind::Global;
            values[index] = static_cast<std::uint32_t>(offset);
            regions.push_back({static_cast<std::uint32_t>(offset), size, packedSize, 0});
        }
    }

    // Lay out the outermost regions in order; vectors contain their _x/_y/_z defs.
    std::sort(regions.begin(), regions.end(), [](const Region & left, const Region & right) {
        return left.offset != right.offset ? left.offset < right.offset : left.size > right.size;
    });

    std::vector<Region> outer;
    for (const Region & region : regions)
    {
        if (!outer.empty() && region.offset < outer.back().offset + outer.back().size)
        {
            if (region.offset + region.size > outer.back().offset + outer.back().size)
            {
                error = "two script globals overlap";
                return false;
            }

            if (region.packedSize != region.size && region.offset != outer.back().offset)
            {
                error = "a string constant inside another global";
                return false;
            }

            continue;
        }

        outer.push_back(region);
    }

    std::vector<std::uint8_t> newGlobals;
    for (Region & region : outer)
    {
        region.newOffset = static_cast<std::uint32_t>(newGlobals.size());
        const std::uint32_t copied = region.packedSize < region.size ? region.packedSize : region.size;
        newGlobals.insert(newGlobals.end(), globals + region.offset, globals + region.offset + copied);
        if (region.packedSize != region.size)
        {
            // The packed copy ends in the string's terminator and zero padding.
            const char * const text = reinterpret_cast<const char *>(globals + region.offset);
            const std::size_t length = ConstantLength(text);
            std::fill(newGlobals.end() - static_cast<std::ptrdiff_t>(copied - length), newGlobals.end(), std::uint8_t(0));
        }
    }

    const auto relocate = [&](std::uint32_t offset, std::uint32_t & relocated) -> bool {
        auto found = std::upper_bound(outer.begin(), outer.end(), offset, [](std::uint32_t value, const Region & region) {
            return value < region.offset;
        });

        if (found == outer.begin())
        {
            return false;
        }

        --found;
        if (offset >= found->offset + (found->size > 0 ? found->size : 1))
        {
            return false;
        }

        relocated = found->newOffset + (offset - found->offset);
        return true;
    };

    for (std::size_t index = 1; index < emittedDefs.size(); ++index)
    {
        if (kinds[index] == format::ValueKind::Global && !relocate(values[index], values[index]))
        {
            error = std::string("global '") + emittedDefs[index]->Name() + "' was not laid out";
            return false;
        }
    }

    // --------------------------------------------------------------------------------------------
    // Records
    // --------------------------------------------------------------------------------------------

    StringPool strings;
    // Def 0, and every def until written, is the placeholder.
    const format::Def placeholder = {0, 0, static_cast<std::uint8_t>(format::ValueKind::None), 0};
    std::vector<format::Def> defs(emittedDefs.size(), placeholder);
    for (std::size_t index = 1; index < emittedDefs.size(); ++index)
    {
        std::uint16_t type = 0;
        if (!findType(emittedDefs[index]->TypeDef(), type))
        {
            return false;
        }

        defs[index] = {values[index], static_cast<std::uint8_t>(emittedDefs[index]->initialized), static_cast<std::uint8_t>(kinds[index]), type};
    }

    std::vector<format::Statement> statements(static_cast<std::size_t>(program.statements.Num()));
    for (int index = 0; index < program.statements.Num(); ++index)
    {
        const statement_t & statement = program.statements[index];
        statements[static_cast<std::size_t>(index)] = {statement.op, indexOf(statement.a), indexOf(statement.b), indexOf(statement.c)};
    }

    std::vector<format::Event> events;
    std::map<const idEventDef *, std::uint16_t> eventIndex;
    std::vector<std::int32_t> parmSizes;
    std::vector<format::Function> functions(static_cast<std::size_t>(program.functions.Num()));
    for (int index = 0; index < program.functions.Num(); ++index)
    {
        const function_t & function = program.functions[index];
        format::Function & record = functions[static_cast<std::size_t>(index)];
        record = {};
        record.name = strings.Add(function.Name());
        record.event = format::kNone;
        if (function.eventdef != nullptr)
        {
            const auto found = eventIndex.find(function.eventdef);
            if (found != eventIndex.end())
            {
                record.event = found->second;
            }
            else
            {
                record.event = static_cast<std::uint16_t>(events.size());
                eventIndex[function.eventdef] = record.event;
                events.push_back({strings.Add(function.eventdef->GetName()), strings.Add(function.eventdef->GetArgFormat()),
                                  static_cast<std::uint8_t>(function.eventdef->GetReturnType()),
                                  static_cast<std::uint8_t>(threadResponds(*function.eventdef) ? 1 : 0), 0});
            }
        }

        if (function.type == nullptr)
        {
            record.type = format::kNone;
        }
        else if (!findType(function.type, record.type))
        {
            return false;
        }

        record.firstStatement = static_cast<std::uint32_t>(function.firstStatement);
        record.numStatements = static_cast<std::uint32_t>(function.numStatements);
        record.parmTotal = static_cast<std::uint32_t>(function.parmTotal);
        record.locals = static_cast<std::uint32_t>(function.locals);
        record.file = static_cast<std::uint16_t>(function.filenum);
        record.parmSizeStart = static_cast<std::uint32_t>(parmSizes.size());
        record.parmSizeCount = static_cast<std::uint16_t>(function.parmSize.Num());
        for (int parm = 0; parm < function.parmSize.Num(); ++parm)
        {
            parmSizes.push_back(function.parmSize[parm]);
        }
    }

    std::vector<std::uint16_t> typeRefs;
    std::vector<std::uint32_t> fieldNames;
    std::vector<std::uint16_t> functionRefs;
    std::vector<format::Type> types(emittedTypes.size());
    for (std::size_t index = 0; index < emittedTypes.size(); ++index)
    {
        const idTypeDef * const type = emittedTypes[index];
        format::Type & record = types[index];
        record = {};
        record.etype = static_cast<std::uint8_t>(type->Type());
        record.aux = format::kNone;
        if (type->auxType != nullptr && !findType(type->auxType, record.aux))
        {
            return false;
        }

        // Function types are shared, so they keep no function's name.
        record.name = type->Type() == ev_function ? 0 : strings.Add(type->Name());
        record.size = static_cast<std::uint32_t>(type->Size());
        record.def = type->Type() == ev_object ? indexOf(type->def) : format::kNone;
        record.parmStart = static_cast<std::uint32_t>(typeRefs.size());
        record.parmCount = static_cast<std::uint16_t>(type->NumParameters());
        for (int parm = 0; parm < type->NumParameters(); ++parm)
        {
            std::uint16_t reference = 0;
            if (!findType(type->GetParmType(parm), reference))
            {
                return false;
            }

            typeRefs.push_back(reference);
            fieldNames.push_back(type->Type() == ev_object ? strings.Add(type->GetParmName(parm)) : 0);
        }

        record.functionStart = static_cast<std::uint32_t>(functionRefs.size());
        record.functionCount = static_cast<std::uint16_t>(type->NumFunctions());
        for (int function = 0; function < type->NumFunctions(); ++function)
        {
            const auto found = functionIndex.find(type->GetFunction(function));
            if (found == functionIndex.end())
            {
                error = std::string("object '") + type->Name() + "' has a virtual function outside the program";
                return false;
            }

            functionRefs.push_back(static_cast<std::uint16_t>(found->second));
        }
    }

    // Lookups, sorted by name with idStr::Cmp as the PS2 searches them, and the scopes they walk.
    std::vector<format::Lookup> lookups;
    std::vector<format::Scope> scopes;
    for (int index = 0; index < defCount; ++index)
    {
        const idVarDef * const def = program.varDefs[index];
        if (lookup[static_cast<std::size_t>(index)])
        {
            lookups.push_back({strings.Add(def->Name()), indexOf(def), def->scope == &def_namespace ? format::kNone : indexOf(def->scope)});
        }

        if (scope[static_cast<std::size_t>(index)])
        {
            scopes.push_back({indexOf(def), def->scope == &def_namespace ? format::kNone : indexOf(def->scope)});
        }
    }

    const std::vector<char> & pool = strings.Bytes();
    std::sort(lookups.begin(), lookups.end(), [&](const format::Lookup & left, const format::Lookup & right) {
        const int order = idStr::Cmp(&pool[left.name], &pool[right.name]);
        if (order != 0)
        {
            return order < 0;
        }

        return std::tie(left.scope, left.def) < std::tie(right.scope, right.def);
    });

    for (std::size_t index = 1; index < lookups.size(); ++index)
    {
        if (lookups[index].name == lookups[index - 1].name && lookups[index].scope == lookups[index - 1].scope)
        {
            error = std::string("two defs named '") + &pool[lookups[index].name] + "' share a scope";
            return false;
        }
    }

    // --------------------------------------------------------------------------------------------
    // Debug: source lines and files
    // --------------------------------------------------------------------------------------------

    StringPool debugStrings;
    std::vector<std::uint16_t> lines(statements.size());
    std::vector<format::FileRun> fileRuns;
    for (int index = 0; index < program.statements.Num(); ++index)
    {
        const statement_t & statement = program.statements[index];
        lines[static_cast<std::size_t>(index)] = statement.linenumber;
        if (fileRuns.empty() || fileRuns.back().file != statement.file)
        {
            fileRuns.push_back({static_cast<std::uint32_t>(index), statement.file});
        }
    }

    std::vector<std::uint32_t> fileNames;
    for (int index = 0; index < program.fileList.Num(); ++index)
    {
        fileNames.push_back(debugStrings.Add(program.fileList[index].c_str()));
    }

    // --------------------------------------------------------------------------------------------
    // Assembly
    // --------------------------------------------------------------------------------------------

    format::Header header = {};
    header.magic = format::kMagic;
    header.version = format::kVersion;

    Output output;
    output.Append(&header, sizeof(header));

    header.defsOffset = output.AppendArray(defs);
    header.defCount = static_cast<std::uint32_t>(defs.size());
    header.statementsOffset = output.AppendArray(statements);
    header.statementCount = static_cast<std::uint32_t>(statements.size());
    header.lookupsOffset = output.AppendArray(lookups);
    header.lookupCount = static_cast<std::uint32_t>(lookups.size());
    header.scopesOffset = output.AppendArray(scopes);
    header.scopeCount = static_cast<std::uint32_t>(scopes.size());
    header.parmSizesOffset = output.AppendArray(parmSizes);
    header.parmSizeCount = static_cast<std::uint32_t>(parmSizes.size());
    header.typeRefsOffset = output.AppendArray(typeRefs);
    header.typeRefCount = static_cast<std::uint32_t>(typeRefs.size());
    header.fieldNamesOffset = output.AppendArray(fieldNames);
    header.functionRefsOffset = output.AppendArray(functionRefs);
    header.functionRefCount = static_cast<std::uint32_t>(functionRefs.size());
    header.globalsOffset = output.AppendArray(newGlobals);
    header.globalBytes = static_cast<std::uint32_t>(newGlobals.size());
    header.stringsOffset = output.Append(pool.data(), pool.size());
    header.stringBytes = static_cast<std::uint32_t>(pool.size());
    output.Align();
    header.residentBytes = output.Size();

    header.functionsOffset = output.AppendArray(functions);
    header.functionCount = static_cast<std::uint32_t>(functions.size());
    header.typesOffset = output.AppendArray(types);
    header.typeCount = static_cast<std::uint32_t>(types.size());
    header.eventsOffset = output.AppendArray(events);
    header.eventCount = static_cast<std::uint32_t>(events.size());
    output.Align();
    header.buildBytes = output.Size() - header.residentBytes;

    const std::uint32_t debugStart = output.Size();
    header.linesOffset = output.AppendArray(lines);
    header.fileRunsOffset = output.AppendArray(fileRuns);
    header.fileRunCount = static_cast<std::uint32_t>(fileRuns.size());
    header.fileNamesOffset = output.AppendArray(fileNames);
    header.fileCount = static_cast<std::uint32_t>(fileNames.size());
    header.debugStringsOffset = output.Append(debugStrings.Bytes().data(), debugStrings.Bytes().size());
    header.debugStringBytes = static_cast<std::uint32_t>(debugStrings.Bytes().size());
    output.Align();
    header.fileBytes = output.Size();

    header.returnDef = indexOf(program.returnDef);
    header.returnStringDef = indexOf(program.returnStringDef);
    header.sysDef = indexOf(program.sysDef);

    if (header.fileBytes > format::kMaxFileBytes)
    {
        error = "the cooked program exceeds the image size limit";
        return false;
    }

    std::vector<std::uint8_t> & bytes = output.Bytes();
    header.residentChecksum = ps2::Fnv1a32(bytes.data() + sizeof(header), debugStart - sizeof(header));
    header.debugChecksum = ps2::Fnv1a32(bytes.data() + debugStart, header.fileBytes - debugStart);
    std::memcpy(bytes.data(), &header, sizeof(header));

    report.fileBytes = header.fileBytes;
    report.residentBytes = header.residentBytes;
    report.buildBytes = header.buildBytes;
    report.debugBytes = header.fileBytes - debugStart;
    report.defs = header.defCount;
    report.statements = header.statementCount;
    report.lookups = header.lookupCount;
    report.scopes = header.scopeCount;
    report.functions = header.functionCount;
    report.types = header.typeCount;
    report.events = header.eventCount;
    report.stringBytes = header.stringBytes;
    report.globalBytes = header.globalBytes;
    report.compiledGlobalBytes = compiledBytes;

    image = std::move(bytes);
    return true;
}

} // namespace cook
