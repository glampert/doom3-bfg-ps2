// ================================================================================================
// File: type_query_tests.cpp
// Brief: Verify rejected sibling/null queries, inherited types, const pointers and separate-unit keys.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/type_query_tests.h"
#include "ps2/system/log.h"

namespace ps2::smoketests
{
namespace
{
bool Check(bool condition, const char * name)
{
    Log(LogLevel::Info, "[D3BFG] CHECK types/%s %s\n", name, condition ? "PASS" : "FAIL");
    return condition;
}
}

bool RunTypeQueryTests()
{
    QueryGrandchild child;
    QuerySibling sibling;
    QueryRoot root;
    QueryRoot * base = &child;
    const QueryRoot * constant = &child;
    const QueryChild * const qualified = CheckedCast<const QueryChild * const>(constant);
    bool passed = Check(CheckedCast<QueryChild *>(base) == &child &&
        CheckedCast<QueryGrandchild *>(base) == &child &&
        CheckedCast<QueryRoot *>(&child) == base &&
        CheckedCast<const QueryChild *>(constant) == &child &&
        qualified != nullptr && qualified->value == 42, "inherited-const");
    passed &= Check(CheckedCast<QueryChild *>(&root) == nullptr &&
        CheckedCast<QueryChild *>(static_cast<QueryRoot *>(&sibling)) == nullptr &&
        CheckedCast<QuerySibling *>(base) == nullptr &&
        CheckedCast<QueryChild *>(static_cast<QueryRoot *>(nullptr)) == nullptr, "rejected-null-sibling");
    QueryRoot * otherUnit = NewQueryChildFromOtherUnit();
    QueryChild * queried = CheckedCast<QueryChild *>(otherUnit);
    passed &= Check(QueryChildTypeKeyFromOtherUnit() == TypeKey<QueryChild>() &&
        TypeKey<QueryChild>() != TypeKey<QuerySibling>() && queried != nullptr &&
        queried->value == 42 && CheckedCast<QuerySibling *>(otherUnit) == nullptr, "translation-unit-identity");
    delete otherUnit;
    return passed;
}

} // namespace ps2::smoketests
