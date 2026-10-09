// ================================================================================================
// File: type_query_tests.h
// Brief: Shared checked-cast probes and cross-translation-unit identity checks for host and EE.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#pragma once

#include "ps2/type_query.h"

namespace ps2::smoketests
{

struct QueryRoot
{
    PS2_TYPE_ROOT(QueryRoot)
    virtual ~QueryRoot() = default;
};
struct QueryChild : QueryRoot
{
    PS2_TYPE_DERIVED(QueryChild, QueryRoot)
    int value = 42;
};
struct QueryGrandchild final : QueryChild
{
    PS2_TYPE_DERIVED(QueryGrandchild, QueryChild)
};
struct QuerySibling final : QueryRoot
{
    PS2_TYPE_DERIVED(QuerySibling, QueryRoot)
};

const void * QueryChildTypeKeyFromOtherUnit();
const void * FileTypeKeyFromOtherUnit();
QueryRoot * NewQueryChildFromOtherUnit();
bool RunTypeQueryTests();
bool RunEngineTypeQueryTests();

} // namespace ps2::smoketests
