// ================================================================================================
// File: heap_tests.cpp
// Brief: Run the same allocation regression tests on the host and the EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/heap_tests.h"

int main()
{
    return ps2::smoketests::RunHeapTests() ? 0 : 1;
}
