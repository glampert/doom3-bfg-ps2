// ================================================================================================
// File: codec_tests.cpp
// Brief: Run the same authored codec probes under host sanitizers; EE smoke remains a separate gate.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/codec_tests.h"

int main()
{
    return ps2::smoketests::RunCodecTests() ? 0 : 1;
}
