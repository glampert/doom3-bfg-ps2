#!/usr/bin/env python3
# ================================================================================================
# File: compiler_explorer.py
# Brief: Compile the host script cook's translation units with CI's MSVC on Compiler Explorer, the
# only MSVC this Mac can reach: each unit becomes one self-contained file, since the service
# compiles a single source and the cook's units pull in about 750 KB of idlib headers.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

"""Check the script cook's units against Compiler Explorer's MSVC (sends sources to godbolt.org).

Each unit is expanded with the headers clang really opens under the cook's defines (`clang++ -H`):
those are inlined at every occurrence inside an #ifndef guard (the first occurrence may sit in a
dead branch), standard and platform headers stay #include lines, comments are blanked and #line
markers keep diagnostics on the real files. Headers under src/neo are wrapped in
#pragma warning(push, 0), standing in for the /external:W0 their SYSTEM include gets. id's units
compile with /w /Zc:strictStrings-, the cook's own with /W4 /WX, as src/tools/CMakeLists.txt does.

    python3 src/tools/scripts/compiler_explorer.py                 # every unit
    python3 src/tools/scripts/compiler_explorer.py cook_engine Lexer
    python3 src/tools/scripts/compiler_explorer.py --expand-only   # write the files, send nothing
"""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import urllib.request

REPO = Path(__file__).resolve().parents[3]
CMAKE_LISTS = REPO / 'src/tools/CMakeLists.txt'
INCLUDE_DIRS = [REPO / 'src', REPO / 'src/neo', REPO / 'src/tools']
SYSTEM_DIRS = [REPO / 'src/neo']
# The Windows runner's toolset (MSVC 14.51, VS 18.6).
DEFAULT_COMPILER = 'vcpp_v19_51_VS18_6_x64'
API = 'https://godbolt.org/api/compiler/%s/compile'
# CMake's Release flags and the cook's MSVC options; /MT is left to the service's /MD (a D9025
# override either way, which /WX does not promote).
BASE_ARGS = ['/std:c++20', '/EHsc', '/O2', '/Ob2', '/fp:precise', '/DWIN32', '/D_WINDOWS', '/DNDEBUG',
             '/D_CRT_SECURE_NO_WARNINGS']
OWN_ARGS = ['/W4', '/WX']
ENGINE_ARGS = ['/w', '/Zc:strictStrings-']

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^">]+)[">]')


# ------------------------------------------------------------------------------------------------
# The cook's units, as CMakeLists.txt lists them
# ------------------------------------------------------------------------------------------------

def CMakeList(text, name):
    match = re.search(r'set\(\s*%s\s+(.*?)\)' % name, text, re.S)
    if match is None:
        sys.exit('compiler_explorer: %s not found in %s' % (name, CMAKE_LISTS))
    return match.group(1).split()


def CookUnits():
    text = CMAKE_LISTS.read_text()
    tools = CMAKE_LISTS.parent

    def Resolve(entry):
        return (tools / entry.replace('${REPO_SOURCE_DIR}', '..')).resolve()

    own = [Resolve(entry) for entry in CMakeList(text, 'SCRIPT_COOK_SOURCES')]
    engine = [Resolve(entry) for entry in CMakeList(text, 'SCRIPT_COOK_ENGINE_SOURCES')]
    match = re.search(r'target_compile_definitions\(\s*script_cook\s+PRIVATE\s+(.*?)\)', text, re.S)
    if match is None:
        sys.exit('compiler_explorer: the script cook\'s definitions not found in %s' % CMAKE_LISTS)
    defines = match.group(1).split()
    return own, engine, defines


# ------------------------------------------------------------------------------------------------
# Expansion
# ------------------------------------------------------------------------------------------------

def StripComments(text):
    """Blank comments, keeping their newlines and leaving string and character literals alone."""
    out = []
    index = 0
    end = len(text)
    while index < end:
        char = text[index]
        if text.startswith('//', index):
            newline = text.find('\n', index)
            index = end if newline < 0 else newline
        elif text.startswith('/*', index):
            close = text.find('*/', index + 2)
            close = end - 2 if close < 0 else close
            out.append(' ' + '\n' * text.count('\n', index, close + 2))
            index = close + 2
        elif char in '"\'':
            scan = index + 1
            while scan < end and text[scan] != char and text[scan] != '\n':
                scan += 2 if text[scan] == '\\' else 1
            out.append(text[index:scan + 1])
            index = scan + 1
        else:
            out.append(char)
            index += 1
    return ''.join(out)


def OpenedHeaders(unit, defines):
    """The project headers clang opens for the unit: only those are inlined."""
    command = ['clang++', '-std=c++20', '-fsyntax-only', '-H', '-w']
    command += ['-D' + define for define in defines]
    command += ['-I' + str(directory) for directory in INCLUDE_DIRS]
    command.append(str(unit))
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit('compiler_explorer: clang cannot compile %s:\n%s' % (unit, result.stderr))
    headers = set()
    for line in result.stderr.splitlines():
        match = re.match(r'^\.+ (.*)$', line)
        if match:
            path = Path(match.group(1)).resolve()
            if REPO in path.parents:
                headers.add(path)
    return headers


def Expand(unit, opened):
    parts = []
    stack = [unit]

    def Resolve(name, quote, directory):
        for candidate in ([directory] if quote == '"' else []) + INCLUDE_DIRS:
            path = (candidate / name).resolve()
            if path.is_file():
                return path
        return None

    def IsSystem(path):
        return any(directory in path.parents for directory in SYSTEM_DIRS)

    def Emit(path, inSystem):
        rel = path.relative_to(REPO).as_posix()
        lines = StripComments(path.read_text(encoding='utf-8', errors='replace')).split('\n')
        blanks = 0
        parts.append('#line 1 "%s"' % rel)
        for number, raw in enumerate(lines, 1):
            match = INCLUDE_RE.match(raw)
            if match and not raw.rstrip().endswith('\\'):
                target = Resolve(match.group(2), match.group(1), path.parent)
                if target is not None and target in opened and target not in stack:
                    guard = 'CE_' + re.sub(r'\W', '_', target.relative_to(REPO).as_posix())
                    wrap = IsSystem(target) and not inSystem
                    parts.extend(['#ifndef ' + guard, '#define ' + guard])
                    if wrap:
                        parts.append('#pragma warning(push, 0)')
                    stack.append(target)
                    Emit(target, inSystem or IsSystem(target))
                    stack.pop()
                    if wrap:
                        parts.append('#pragma warning(pop)')
                    parts.append('#endif')
                    parts.append('#line %d "%s"' % (number + 1, rel))
                    blanks = 0
                    continue

            line = raw.strip()
            if line == '#pragma once':
                line = ''
            # A blank line ends a macro continuation, so it stays inside one.
            if not line and not (parts and parts[-1].endswith('\\')):
                blanks += 1
                continue
            if blanks > 2:
                parts.append('#line %d "%s"' % (number, rel))
            else:
                parts.extend([''] * blanks)
            blanks = 0
            parts.append(line)

    Emit(unit, False)
    return '\n'.join(parts) + '\n'


def CheckExpansion(path, defines):
    """The expanded file must still compile alone with clang, or the expansion lost something."""
    command = ['clang++', '-std=c++20', '-fsyntax-only', '-w', '-x', 'c++'] + ['-D' + d for d in defines]
    result = subprocess.run(command + [str(path)], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit('compiler_explorer: the expansion of %s does not compile:\n%s' % (path.name, result.stderr))


# ------------------------------------------------------------------------------------------------
# Compiler Explorer
# ------------------------------------------------------------------------------------------------

def Compile(compiler, source, arguments):
    body = {
        'source': source,
        'options': {
            'userArguments': ' '.join(arguments),
            'filters': {'binary': False, 'execute': False, 'labels': True, 'directives': True,
                        'commentOnly': True, 'intel': True, 'demangle': True},
            'compilerOptions': {'skipAsm': True},
        },
        'lang': 'c++',
        'allowStoreCodeDebug': False,
    }
    request = urllib.request.Request(API % compiler, data=json.dumps(body).encode(), method='POST',
                                     headers={'Content-Type': 'application/json', 'Accept': 'application/json',
                                              'User-Agent': 'host-tool-check'})
    with urllib.request.urlopen(request, timeout=300) as response:
        result = json.load(response)
    lines = [line.get('text', '') for stream in ('stdout', 'stderr') for line in result.get(stream, [])]
    # cl echoes the file name it compiles; D9025 is the /MD-/MT override noted above.
    diagnostics = [line for line in lines if line.strip() and line.strip() != 'example.cpp' and 'D9025' not in line]
    return result.get('code', -1), diagnostics


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('units', nargs='*', help='unit names (file stems) to check; every unit by default')
    parser.add_argument('--compiler', default=DEFAULT_COMPILER, help='Compiler Explorer compiler id')
    parser.add_argument('--out-dir', type=Path, default=REPO / 'build/compiler-explorer',
                        help='where the expanded files are written')
    parser.add_argument('--expand-only', action='store_true', help='write the expanded files and send nothing')
    args = parser.parse_args()

    own, engine, defines = CookUnits()
    units = [(unit, OWN_ARGS) for unit in own] + [(unit, ENGINE_ARGS) for unit in engine]
    if args.units:
        known = {unit.stem for unit, _ in units}
        unknown = sorted(set(args.units) - known)
        if unknown:
            sys.exit('compiler_explorer: unknown units %s (known: %s)' % (', '.join(unknown), ', '.join(sorted(known))))
        units = [(unit, flags) for unit, flags in units if unit.stem in args.units]

    args.out_dir.mkdir(parents=True, exist_ok=True)
    failed = []
    for index, (unit, flags) in enumerate(units):
        expanded = args.out_dir / (unit.stem + '.cpp')
        expanded.write_text(Expand(unit, OpenedHeaders(unit, defines)), encoding='utf-8')
        CheckExpansion(expanded, defines)
        if args.expand_only:
            print('%-24s %8d bytes' % (unit.name, expanded.stat().st_size))
            continue

        if index > 0:
            time.sleep(1)  # one request at a time, gently
        code, diagnostics = Compile(args.compiler, expanded.read_text(encoding='utf-8'),
                                    BASE_ARGS + flags + ['/D' + define for define in defines])
        print('%-24s %-28s %s' % (unit.name, ' '.join(flags), 'ok' if code == 0 and not diagnostics else 'FAILED'))
        for line in diagnostics:
            print('    ' + line)
        if code != 0 or diagnostics:
            failed.append(unit.name)

    if failed:
        print('%d of %d units failed: %s' % (len(failed), len(units), ', '.join(failed)))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
