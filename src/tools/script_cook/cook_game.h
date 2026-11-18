// ================================================================================================
// File: cook_game.h
// Brief: The game surface id's script compiler sees in the host script cook, in place of
// Game_local.h: the event table read from events.txt instead of the game classes' registry, and
// stand-ins for the few game objects the compiler and program reach.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#if !defined(ID_SCRIPT_COOK)
#error "cook_game.h is the host script cook's replacement for Game_local.h"
#endif

#include <cstdarg>

// The compiler's implicit include, as d3xp/Game.h defines it (Game.h needs the whole engine).
#define SCRIPT_DEFAULTDEFS "script/doom_defs.script"

// The format characters of gamesys/Event.h, which the cook cannot include (it declares the
// game's own idEventDef). The PS2 checks the cooked program's events against its registry.
#define D_EVENT_MAXARGS 8
#define D_EVENT_VOID ((char)0)
#define D_EVENT_INTEGER 'd'
#define D_EVENT_FLOAT 'f'
#define D_EVENT_VECTOR 'v'
#define D_EVENT_STRING 's'
#define D_EVENT_ENTITY 'e'
#define D_EVENT_ENTITY_NULL 'E'
#define D_EVENT_TRACE 't'

// One events.txt entry, with the part of the game's idEventDef interface the compiler uses.
class idEventDef
{
public:
    idEventDef(const char * name, const char * format, char returnType, bool thread);

    const char * GetName() const
    {
        return m_name.c_str();
    }

    const char * GetArgFormat() const
    {
        return m_format.c_str();
    }

    char GetReturnType() const
    {
        return m_returnType;
    }

    int GetNumArgs() const
    {
        return m_format.Length();
    }

    bool ThreadResponds() const
    {
        return m_thread;
    }

    // Returns null for a name events.txt does not list.
    static const idEventDef * FindEvent(const char * name);

private:
    idStr m_name;
    idStr m_format;
    char m_returnType;
    bool m_thread;
};

class idEntity
{
public:
    int entityNumber;
};

class idSaveGame
{
public:
    void WriteInt(int value);
    void WriteByte(byte value);
    void WriteString(const char * string);
    void Write(const void * data, int length);
    [[noreturn]] void Error(const char * format, ...);
};

class idRestoreGame
{
public:
    void ReadInt(int & value);
    void ReadByte(byte & value);
    void ReadString(idStr & string);
    void Read(void * data, int length);
    [[noreturn]] void Error(const char * format, ...);
};

[[noreturn]] void gameError(const char * format, ...);

#include "d3xp/script/Script_Program.h"
#include "d3xp/script/Script_Compiler.h"

// The thread class's part the program reaches: restarting threads (none exist in the cook) and the
// event-response query, answered from events.txt.
class idThread
{
public:
    struct CookType
    {
        bool RespondsTo(const idEventDef & event) const
        {
            return event.ThreadResponds();
        }
    };

    static void Restart()
    {
    }

    static CookType Type;
};

// Only Disassemble reads it, and the cook never disassembles.
struct idCookDisasmCVar
{
    bool GetBool() const
    {
        return false;
    }
};

extern idCookDisasmCVar g_disasm;

class idCookGameLocal
{
public:
    idProgram program;

    void Printf(const char * format, ...);
    void DPrintf(const char * format, ...);
    void Warning(const char * format, ...);
    [[noreturn]] void Error(const char * format, ...);
};

extern idCookGameLocal gameLocal;
