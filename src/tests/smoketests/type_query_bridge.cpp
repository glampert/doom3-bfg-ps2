// ================================================================================================
// File: type_query_bridge.cpp
// Brief: Ensure type tokens and virtual queries agree across independently compiled source files.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/type_query_tests.h"

class idFile;

namespace ps2::smoketests
{

const void * QueryChildTypeKeyFromOtherUnit() { return TypeKey<QueryChild>(); }
const void * FileTypeKeyFromOtherUnit() { return TypeKey<idFile>(); }
QueryRoot * NewQueryChildFromOtherUnit() { return new QueryChild; }

} // namespace ps2::smoketests
