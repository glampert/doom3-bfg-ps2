// ================================================================================================
// File: model_import.cpp
// Brief: Reject desktop source-model import until host conversion supplies target runtime content.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "ps2/system/log.h"
#include <idlib/precompiled.h>

// Native declarations preserve the ABI without importing their source-format parsers.
#include <renderer/Model_ase.h>
#include <renderer/Model_lwo.h>
#include <renderer/Model_ma.h>

namespace
{
[[noreturn]] PS2_COLD_FUNC void Unsupported(const char * method)
{
    ps2::FatalError("source-model import unavailable: %s; use host-converted runtime content", method);
}
} // namespace

aseModel_t * ASE_Load(const char *) { Unsupported("ASE_Load"); }
void ASE_Free(aseModel_t *) { Unsupported("ASE_Free"); }
maModel_t * MA_Load(const char *) { Unsupported("MA_Load"); }
void MA_Free(maModel_t *) { Unsupported("MA_Free"); }
lwObject * lwGetObject(const char *, unsigned int *, int *) { Unsupported("lwGetObject"); }
void lwFreeObject(lwObject *) { Unsupported("lwFreeObject"); }
