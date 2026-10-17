// ================================================================================================
// File: precompiled.h
// Brief: Supply native allocation tag values without linking engine initialization into host tests.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

enum
{
#define MEM_TAG(name) TAG_##name,
#include "neo/idlib/sys/sys_alloc_tags.h"
#undef MEM_TAG
};
