// ================================================================================================
// File: heap_tests.cpp
// Brief: Run the same allocation regression tests on the host and the EE.
// SPDX-License-Identifier: GPL-3.0-or-later
// ================================================================================================

#include "tests/smoketests/heap_tests.h"

int main()
{
    return ps2::smoketests::RunHeapTests() ? 0 : 1;
}
