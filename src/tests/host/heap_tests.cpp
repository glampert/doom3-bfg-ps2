// ================================================================================================
// File: heap_tests.cpp
// Brief: Run the same allocation regression tests on the host and the EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/heap_tests.h"
#include "tests/smoketests/class_alloc_tests.h"
#include "tests/smoketests/type_query_tests.h"

int main()
{
    const bool heapPassed = ps2::smoketests::RunHeapTests();
    const bool classPassed = ps2::smoketests::RunClassAllocTests();
    const bool typesPassed = ps2::smoketests::RunTypeQueryTests();
    return heapPassed && classPassed && typesPassed ? 0 : 1;
}
