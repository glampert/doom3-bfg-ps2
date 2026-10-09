#!/usr/bin/env python3
# ================================================================================================
# File: test_type_query.py
# Brief: Ensure portable queries reject unsafe casts at compile time and retain desktop RTTI behavior.
# This source code is released under the GNU GPL-3.0-or-later license.
# ================================================================================================

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
DECLARATIONS = '''
#include "ps2/type_query.h"
struct Root { PS2_TYPE_ROOT(Root) virtual ~Root() = default; };
struct Child : Root { PS2_TYPE_DERIVED(Child, Root) };
struct Sibling : Root { PS2_TYPE_DERIVED(Sibling, Root) };
struct Unregistered : Child {};
'''


class TypeQueryCompileTests(unittest.TestCase):
    def reject(self, expression):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "query.cpp"
            source.write_text(DECLARATIONS + expression)
            result = subprocess.run(["clang++", "-std=c++20", "-fno-rtti", "-fno-exceptions",
                                     "-DID_HOST_TEST", "-I" + str(ROOT / "src"),
                                     "-fsyntax-only", str(source)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("type_query.h", result.stderr)

    def test_unregistered_target_rejected(self):
        self.reject("Unregistered * Query(Root * p) { return ps2::CheckedCast<Unregistered *>(p); }")

    def test_const_removal_rejected(self):
        self.reject("Child * Query(const Root * p) { return ps2::CheckedCast<Child *>(p); }")

    def test_cross_cast_rejected(self):
        self.reject("Sibling * Query(Child * p) { return ps2::CheckedCast<Sibling *>(p); }")

    def test_desktop_rtti_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "query.cpp"
            executable = Path(directory) / "query"
            source.write_text(DECLARATIONS + '''
int main() {
    Child child;
    Root * base = &child;
    const Root * constant = base;
    return !(ps2::CheckedCast<Child *>(base) == &child &&
             ps2::CheckedCast<const Child *>(constant) == &child &&
             ps2::CheckedCast<Sibling *>(base) == nullptr &&
             ps2::CheckedCast<Child *>(static_cast<Root *>(nullptr)) == nullptr);
}
''')
            subprocess.run(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(ROOT / "src"), str(source), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
