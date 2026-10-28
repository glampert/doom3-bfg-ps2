// ================================================================================================
// File: type_query_tests.cpp
// Brief: Verify rejected sibling/null queries, inherited types, const pointers and separate-unit keys.
// This source code is released under the GNU GPL-3.0-or-later license.
// ================================================================================================

#include "tests/smoketests/type_query_tests.h"
#include "ps2/system/log.h"

namespace ps2::tests
{
namespace
{
bool Check(bool condition, const char * name)
{
    Log(LogLevel::Info, "[D3BFG] CHECK types/%s %s\n", name, condition ? "PASS" : "FAIL");
    return condition;
}
} // namespace

bool RunTypeQueryTests()
{
    QueryGrandchild child;
    QuerySibling sibling;
    QueryRoot root;
    QueryRoot * base = &child;
    const QueryRoot * constant = &child;
    const QueryChild * const qualified = DynCast<const QueryChild * const>(constant);
    bool passed = Check(DynCast<QueryChild *>(base) == &child &&
                        DynCast<QueryGrandchild *>(base) == &child &&
                        DynCast<QueryRoot *>(&child) == base &&
                        DynCast<const QueryChild *>(constant) == &child &&
                        qualified != nullptr && qualified->value == 42,
                        "inherited-const");
    passed &= Check(DynCast<QueryChild *>(&root) == nullptr &&
                    DynCast<QueryChild *>(static_cast<QueryRoot *>(&sibling)) == nullptr &&
                    DynCast<QuerySibling *>(base) == nullptr &&
                    DynCast<QueryChild *>(static_cast<QueryRoot *>(nullptr)) == nullptr,
                    "rejected-null-sibling");
    QueryRoot * otherUnit = NewQueryChildFromOtherUnit();
    QueryChild * queried = DynCast<QueryChild *>(otherUnit);
    passed &= Check(QueryChildTypeKeyFromOtherUnit() == TypeKey<QueryChild>() &&
                    TypeKey<QueryChild>() != TypeKey<QuerySibling>() && queried != nullptr &&
                    queried->value == 42 && DynCast<QuerySibling *>(otherUnit) == nullptr,
                    "translation-unit-identity");
    delete otherUnit;
    return passed;
}

} // namespace ps2::tests
