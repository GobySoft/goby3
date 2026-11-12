// Copyright 2020-2023:
//   GobySoft, LLC (2013-)
//   Community contributors (see AUTHORS file)
// File authors:
//   Toby Schneider <toby@gobysoft.org>
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include <functional> // for function
#include <iostream>   // for operator<<, basic...
#include <iterator>   // for operator!=, rever...
#include <limits>     // for numeric_limits
#include <map>        // for _Rb_tree_const_it...
#include <memory>     // for allocator, unique...
#include <set>        // for set
#include <stdexcept>  // for runtime_error
#include <stdlib.h>   // for exit, EXIT_FAILURE
#include <string>     // for string, basic_string
#include <utility>    // for pair, make_pair

#include <boost/algorithm/string/erase.hpp>        // for erase_all
#include <boost/iterator/iterator_traits.hpp>      // for iterator_value<>:...
#include <clang/AST/DeclCXX.h>                     // for CXXRecordDecl
#include <clang/AST/Expr.h>                        // for Expr, IntegerLiteral
#include <clang/AST/ExprCXX.h>                     // for CXXMemberCallExpr
#include <clang/AST/PrettyPrinter.h>               // for PrintingPolicy
#include <clang/AST/TemplateBase.h>                // for TemplateArgument
#include <clang/AST/Type.h>                        // for QualType, Type
#include <clang/ASTMatchers/ASTMatchersInternal.h> // for VariadicFunction
#include <clang/Basic/LangOptions.h>               // for LangOptions
#include <llvm/ADT/APInt.h>                        // for APInt
#include <llvm/ADT/APSInt.h>                       // for APSInt
#include <llvm/ADT/StringRef.h>                    // for StringRef
#include <llvm/Support/raw_ostream.h>              // for raw_string_ostream
#include <yaml-cpp/emitter.h>                      // for operator<<, Emitter

#include "actions.h"                                     // for generate
#include "goby/middleware/marshalling/interface.h"       // for MarshallingScheme
#include "goby/middleware/tool/interface/pubsub_entry.h" // for Layer, PubSubEntry
#include "goby/middleware/tool/interface/yaml_raii.h"    // for YMap, YSeq
#include "goby/middleware/transport/interface.h"         // for Necessity, Necess...
#include "clang/ASTMatchers/ASTMatchFinder.h"            // for MatchFinder::Matc...
#include "clang/ASTMatchers/ASTMatchers.h"               // for hasName, callee
#include "clang/Tooling/Tooling.h"                       // for newFrontendAction...

using goby::middleware::tool::interface::Layer;
using goby::middleware::tool::interface::PubSubEntry;

// picks out the object (goby::middleware::Thread) that made the publish/subscribe call
auto calling_thread_matcher()
{
    using namespace clang::ast_matchers;
    return on(expr(
        anyOf(
            // Thread: pull out what "this" in "this->interprocess()" when "this" is derived from goby::middleware::Thread
            cxxMemberCallExpr(on(hasType(
                pointsTo(hasUnqualifiedDesugaredType(recordType(hasDeclaration(cxxRecordDecl(
                    cxxRecordDecl().bind("on_thread_decl"),
                    isDerivedFrom(cxxRecordDecl(hasName("::goby::middleware::Thread"))))))))))),
            // Thread: match this->goby().interprocess() or similar chains
            hasDescendant(cxxMemberCallExpr(on(hasType(pointsTo(cxxRecordDecl(
                cxxRecordDecl().bind("on_indirect_thread_decl"),
                isDerivedFrom(cxxRecordDecl(hasName("::goby::middleware::Thread"))))))))),
            // also allow direct calls to publish/subscribe
            expr().bind("on_expr")),
        // call is on an instantiation of a class derived from StaticTransporterInterface
        hasType(hasUnqualifiedDesugaredType(recordType(hasDeclaration(cxxRecordDecl(
            decl().bind("on_type_decl"),
            isDerivedFrom(cxxRecordDecl(hasName("::goby::middleware::StaticTransporterInterface"))),
            unless(hasName("::goby::middleware::NullTransporter")))))))));
}

// if on_thread_decl or on_thread_direct_decl fails, use this to try to determine the class that contains this statement
auto containing_class_matcher()
{
    using namespace clang::ast_matchers;
    return anyOf(hasAncestor(cxxRecordDecl().bind(
                     "containing_class_decl")), // should be "optionally" but this isn't in clang 6
                 expr());
}

::clang::ast_matchers::StatementMatcher pubsub_matcher(std::string method)
{
    using namespace clang::ast_matchers;
    auto calling_thread_matcher = ::calling_thread_matcher();

    // picks out string argument to Group constructor
    auto group_string_arg_matcher =
        anyOf(stringLiteral().bind("group_string_arg"),
              declRefExpr(hasDeclaration(
                  varDecl(hasDescendant(stringLiteral().bind("group_string_arg"))))));
    // picks out integer argument to Group constructor
    auto group_int_arg_matcher = anyOf(
        integerLiteral().bind("group_int_arg"),
        declRefExpr(hasDeclaration(varDecl(hasDescendant(integerLiteral().bind("group_int_arg"))))),
        expr().bind("group_int_arg"));

    // picks out the parameters of the publish/subscribe call
    auto base_pubsub_parameters_matcher = cxxMethodDecl(
        // "publish" or "subscribe"
        hasName(method),
        // Group (must refer to goby::middleware::Group)
        hasTemplateArgument(
            0, templateArgument(refersToDeclaration(varDecl(
                   decl().bind("group_decl"),
                   hasType(cxxRecordDecl(hasName("::goby::middleware::Group"))),
                   // find the actual group argument and bind it
                   hasDescendant(cxxConstructExpr(
                       hasArgument(0, anyOf(group_string_arg_matcher, group_int_arg_matcher)),
                       hasArgument(1, group_int_arg_matcher))))))),
        // Type (no restrictions)
        hasTemplateArgument(1, templateArgument().bind("type_arg")),
        // Scheme (must be int)
        hasTemplateArgument(2, templateArgument(templateArgument().bind("scheme_arg"),
                                                refersToIntegralType(qualType(asString("int"))))));

    auto containing_class_matcher = ::containing_class_matcher();

    if (method == "publish")
    {
        auto publish_parameters_matcher = callee(base_pubsub_parameters_matcher);
        return cxxMemberCallExpr(expr().bind("pubsub_call_expr"), calling_thread_matcher,
                                 publish_parameters_matcher, containing_class_matcher);
    }
    else if (method == "subscribe")
    {
        // subscribe also has a simplified template version that infers arguments from the function parameters; we need to match the subsequent call to the full subscribe
        auto subscribe_parameters_matcher_base =
            cxxMethodDecl(base_pubsub_parameters_matcher,
                          hasTemplateArgument(3, templateArgument().bind("necessity_arg")));

        auto subscribe_parameters_matcher = anyOf(
            callee(subscribe_parameters_matcher_base),
            // matches "template <const Group& group, typename Func> void subscribe(Func f)"
            callee(cxxMethodDecl(
                hasName(method),
                // simplified version calls full subscribe, so pull the parameters out from this call
                hasDescendant(cxxMemberCallExpr(callee(subscribe_parameters_matcher_base))))));

        return cxxMemberCallExpr(expr().bind("pubsub_call_expr"), calling_thread_matcher,
                                 subscribe_parameters_matcher, containing_class_matcher);
    }
    else if (method == "subscribe_type_regex")
    {
        auto subscribe_regex_parameters_matcher = callee(cxxMethodDecl(
            base_pubsub_parameters_matcher,
            anyOf(hasTemplateArgument(3, templateArgument().bind("necessity_arg")), anything())));
        return cxxMemberCallExpr(expr().bind("pubsub_call_expr"), calling_thread_matcher,
                                 subscribe_regex_parameters_matcher, containing_class_matcher);
    }
    else
    {
        throw(
            std::runtime_error("method must be 'publish', 'subscribe', or 'subscribe_type_regex'"));
    }
}

// publish_dynamic<Data, scheme>(data, group) and subscribe_dynamic<Data, scheme, necessity>(f, group)
// where the group argument is a DynamicGroup; Goby's own forwarding calls pass a const Group&
::clang::ast_matchers::StatementMatcher pubsub_dynamic_matcher(std::string method)
{
    using namespace clang::ast_matchers;
    auto dynamic_group = expr(hasType(hasUnqualifiedDesugaredType(
        recordType(hasDeclaration(cxxRecordDecl(hasName("::goby::middleware::DynamicGroup")))))));

    return cxxMemberCallExpr(
        expr().bind("pubsub_call_expr"), calling_thread_matcher(),
        callee(cxxMethodDecl(
            hasName(method), hasTemplateArgument(0, templateArgument().bind("type_arg")),
            hasTemplateArgument(1,
                                templateArgument(templateArgument().bind("scheme_arg"),
                                                 refersToIntegralType(qualType(asString("int"))))),
            anyOf(hasTemplateArgument(2, templateArgument().bind("necessity_arg")), anything()))),
        hasArgument(1, ignoringImplicit(dynamic_group.bind("dynamic_group_expr"))),
        containing_class_matcher());
}

// ptr.reset(new DynamicGroup(...)), for groups held in a std::unique_ptr or std::shared_ptr
::clang::ast_matchers::StatementMatcher dynamic_group_reset_matcher()
{
    using namespace clang::ast_matchers;
    return cxxMemberCallExpr(
        callee(cxxMethodDecl(hasName("reset"))), on(expr().bind("ptr_expr")),
        hasArgument(0, ignoringImplicit(cxxNewExpr(hasType(pointsTo(cxxRecordDecl(hasName(
                                                       "::goby::middleware::DynamicGroup")))))
                                            .bind("new_expr"))));
}

namespace
{
/// \brief What goby_clang_tool can tell statically about a DynamicGroup
struct DynamicGroupInfo
{
    // group string, or "*" if unknown
    std::string group{"*"};
    bool numeric_runtime{false};
    // the C++ expression, when the group is unknown
    std::string expr;
    // the group lives behind a smart pointer set elsewhere (perhaps in another translation unit)
    std::string pointer;
};

std::string print(const clang::Expr* e)
{
    std::string out;
    llvm::raw_string_ostream os(out);
    e->printPretty(os, nullptr, clang::PrintingPolicy(clang::LangOptions()));
    os.flush();
    if (out.size() > 80)
        out = out.substr(0, 77) + "...";
    return out;
}

const clang::Expr* strip(const clang::Expr* e)
{
    for (;;)
    {
        const clang::Expr* next = e->IgnoreImplicit()->IgnoreParens();
        if (const auto* cast = llvm::dyn_cast<clang::CXXFunctionalCastExpr>(next))
            next = cast->getSubExpr();
        if (const auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(next))
        {
            // copies and moves of a DynamicGroup
            if (construct->getConstructor()->isCopyOrMoveConstructor() &&
                construct->getNumArgs() > 0)
                next = construct->getArg(0);
        }
        if (next == e)
            return e;
        e = next;
    }
}

std::string pointer_key(const clang::Expr* e)
{
    e = strip(e);
    if (const auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(e))
        return ref->getDecl()->getQualifiedNameAsString();
    if (const auto* member = llvm::dyn_cast<clang::MemberExpr>(e))
        return member->getMemberDecl()->getQualifiedNameAsString();
    return "";
}

class DynamicGroupClassifier
{
  public:
    explicit DynamicGroupClassifier(clang::ASTContext& ctx) : ctx_(ctx) {}

    DynamicGroupInfo classify(const clang::Expr* group_expr)
    {
        DynamicGroupInfo info;
        info.expr = print(strip(group_expr));
        if (!follow(group_expr, info, 0))
            info.group = "*";
        return info;
    }

  private:
    bool follow(const clang::Expr* e, DynamicGroupInfo& info, int depth)
    {
        if (depth > 8)
            return false;
        e = strip(e);

        if (const auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(e))
            return from_constructor(construct, info);

        if (const auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(e))
        {
            const auto* var = llvm::dyn_cast<clang::VarDecl>(ref->getDecl());
            if (var && !llvm::isa<clang::ParmVarDecl>(var) && var->getInit())
                return follow(var->getInit(), info, depth + 1);
            return false;
        }

        if (const auto* member = llvm::dyn_cast<clang::MemberExpr>(e))
        {
            const auto* field = llvm::dyn_cast<clang::FieldDecl>(member->getMemberDecl());
            if (!field)
                return false;
            if (field->hasInClassInitializer() && field->getInClassInitializer())
                return follow(field->getInClassInitializer(), info, depth + 1);
            const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(field->getParent());
            if (!record)
                return false;
            for (const auto* ctor : record->ctors())
            {
                for (const auto* init : ctor->inits())
                {
                    if (init->getMember() == field && init->getInit())
                        return follow(init->getInit(), info, depth + 1);
                }
            }
            return false;
        }

        // *ptr: a smart pointer set with reset(new DynamicGroup(...)), maybe elsewhere
        if (const auto* op = llvm::dyn_cast<clang::CXXOperatorCallExpr>(e))
        {
            if (op->getOperator() == clang::OO_Star && op->getNumArgs() == 1)
            {
                info.pointer = pointer_key(op->getArg(0));
                return false;
            }
        }

        // a helper function that returns a DynamicGroup
        if (const auto* call = llvm::dyn_cast<clang::CallExpr>(e))
        {
            const auto* fn = call->getDirectCallee();
            if (fn && fn->getBody())
            {
                for (const auto* stmt : fn->getBody()->children())
                {
                    if (const auto* ret = llvm::dyn_cast_or_null<clang::ReturnStmt>(stmt))
                        return ret->getRetValue() && follow(ret->getRetValue(), info, depth + 1);
                }
            }
        }
        return false;
    }

    bool from_constructor(const clang::CXXConstructExpr* construct, DynamicGroupInfo& info)
    {
        const auto* ctor = construct->getConstructor();
        if (!ctor ||
            ctor->getParent()->getQualifiedNameAsString() != "goby::middleware::DynamicGroup" ||
            construct->getNumArgs() == 0)
            return false;

        // DynamicGroup(std::uint32_t)
        if (ctor->getParamDecl(0)->getType()->isIntegerType())
        {
            clang::Expr::EvalResult value;
            if (!construct->getArg(0)->EvaluateAsInt(value, ctx_))
                return false;
            info.group = std::to_string(value.Val.getInt().getZExtValue());
            return true;
        }

        // DynamicGroup(const std::string&, std::uint32_t = invalid_numeric_group)
        std::string name;
        if (!string_value(construct->getArg(0), name, 0))
            return false;
        info.group = name;
        if (construct->getNumArgs() > 1 &&
            !llvm::isa<clang::CXXDefaultArgExpr>(construct->getArg(1)))
        {
            clang::Expr::EvalResult value;
            if (construct->getArg(1)->EvaluateAsInt(value, ctx_))
                info.group += ";" + std::to_string(value.Val.getInt().getZExtValue());
            else
                info.numeric_runtime = true;
        }
        return true;
    }

    // a string literal, a constant string, or a (constexpr) Group converted to std::string
    bool string_value(const clang::Expr* e, std::string& value, int depth)
    {
        if (depth > 8)
            return false;
        e = e->IgnoreImplicit()->IgnoreParens();

        if (const auto* literal = llvm::dyn_cast<clang::StringLiteral>(e))
        {
            value = literal->getString().str();
            return true;
        }
        if (const auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(e))
        {
            if (construct->getConstructor()->getParent()->getQualifiedNameAsString() ==
                "goby::middleware::Group")
                return group_value(construct, value);
            return construct->getNumArgs() > 0 &&
                   string_value(construct->getArg(0), value, depth + 1);
        }
        if (const auto* call = llvm::dyn_cast<clang::CXXMemberCallExpr>(e))
        {
            // Group::operator std::string()
            if (llvm::isa<clang::CXXConversionDecl>(call->getMethodDecl()))
                return string_value(call->getImplicitObjectArgument(), value, depth + 1);
            return false;
        }
        if (const auto* ref = llvm::dyn_cast<clang::DeclRefExpr>(e))
        {
            const auto* var = llvm::dyn_cast<clang::VarDecl>(ref->getDecl());
            if (var && !llvm::isa<clang::ParmVarDecl>(var) && var->getInit() &&
                (var->isConstexpr() || var->getType().isConstQualified()))
                return string_value(var->getInit(), value, depth + 1);
        }
        return false;
    }

    bool group_value(const clang::CXXConstructExpr* group, std::string& value)
    {
        if (group->getNumArgs() == 0 || !string_value(group->getArg(0), value, 1))
            return false;
        if (group->getNumArgs() > 1 && !llvm::isa<clang::CXXDefaultArgExpr>(group->getArg(1)))
        {
            clang::Expr::EvalResult numeric;
            if (group->getArg(1)->EvaluateAsInt(numeric, ctx_))
                value += ";" + std::to_string(numeric.Val.getInt().getZExtValue());
        }
        return true;
    }

    clang::ASTContext& ctx_;
};
} // namespace

/// \brief Records reset(new DynamicGroup(...)) on smart pointers, across translation units
class DynamicGroupResets : public ::clang::ast_matchers::MatchFinder::MatchCallback
{
  public:
    void run(const ::clang::ast_matchers::MatchFinder::MatchResult& Result) override
    {
        const auto* ptr_expr = Result.Nodes.getNodeAs<clang::Expr>("ptr_expr");
        const auto* new_expr = Result.Nodes.getNodeAs<clang::CXXNewExpr>("new_expr");
        // with guaranteed copy elision, new DynamicGroup(f()) has f() as its initializer and no
        // constructor call
        if (!ptr_expr || !new_expr || !new_expr->getInitializer())
            return;
        auto key = pointer_key(ptr_expr);
        if (key.empty())
            return;
        auto info = DynamicGroupClassifier(*Result.Context).classify(new_expr->getInitializer());
        groups_.emplace(key, info);
    }

    const DynamicGroupInfo* find(const std::string& key) const
    {
        auto it = groups_.find(key);
        return it == groups_.end() ? nullptr : &it->second;
    }

  private:
    std::map<std::string, DynamicGroupInfo> groups_;
};

class PubSubAggregator : public ::clang::ast_matchers::MatchFinder::MatchCallback
{
  public:
    virtual void run(const ::clang::ast_matchers::MatchFinder::MatchResult& Result)
    {
        // we know that this call is from a goby::middleware::Thread
        bool thread_is_known = true;

        // the function call itself (e.g. interprocess().publish<...>(...));
        const auto* pubsub_call_expr =
            Result.Nodes.getNodeAs<clang::CXXMemberCallExpr>("pubsub_call_expr");

        const auto* group_string_lit =
            Result.Nodes.getNodeAs<clang::StringLiteral>("group_string_arg");
        const auto* group_int_lit = Result.Nodes.getNodeAs<clang::IntegerLiteral>("group_int_arg");

        //        const auto* group_decl = Result.Nodes.getNodeAs<clang::Decl>("group_decl");

        const auto* type_arg = Result.Nodes.getNodeAs<clang::TemplateArgument>("type_arg");
        const auto* scheme_arg = Result.Nodes.getNodeAs<clang::TemplateArgument>("scheme_arg");
        const auto* necessity_arg =
            Result.Nodes.getNodeAs<clang::TemplateArgument>("necessity_arg");
        const auto* on_type_decl = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("on_type_decl");
        const auto* on_thread_decl = Result.Nodes.getNodeAs<clang::CXXRecordDecl>("on_thread_decl");

        // const auto* on_expr = Result.Nodes.getNodeAs<clang::Expr>("on_expr");

        const auto* containing_class_decl =
            Result.Nodes.getNodeAs<clang::CXXRecordDecl>("containing_class_decl");

        if (!on_thread_decl)
            on_thread_decl =
                Result.Nodes.getNodeAs<clang::CXXRecordDecl>("on_indirect_thread_decl");

        if (!on_thread_decl)
        {
            thread_is_known = false;
            on_thread_decl = containing_class_decl;
        }

        const auto* dynamic_group_expr = Result.Nodes.getNodeAs<clang::Expr>("dynamic_group_expr");

        if (!pubsub_call_expr || !on_type_decl ||
            (!dynamic_group_expr && !group_string_lit && !group_int_lit))
            return;

        const std::string layer_type = on_type_decl->getQualifiedNameAsString();
        Layer layer = Layer::UNKNOWN;

        if (layer_type.find("InterThread") != std::string::npos)
            layer = Layer::INTERTHREAD;
        else if (layer_type.find("InterProcess") != std::string::npos)
            layer = Layer::INTERPROCESS;
        else if (layer_type.find("InterModule") != std::string::npos)
            layer = Layer::INTERMODULE;
        else if (layer_type.find("InterVehicle") != std::string::npos)
            layer = Layer::INTERVEHICLE;

        std::string thread = "unknown";
        std::set<std::string> bases;
        if (on_thread_decl)
        {
            thread = as_string(*on_thread_decl);
            insert_bases(bases, on_thread_decl);
        }

        bases_[thread] = bases;

        for (auto base : bases) parents_[base].insert(thread);

        DynamicGroupInfo dynamic;
        if (dynamic_group_expr)
            dynamic = DynamicGroupClassifier(*Result.Context).classify(dynamic_group_expr);

        std::string group = "unknown";
        if (dynamic_group_expr)
            group = dynamic.group;
        else if (group_string_lit)
            group = group_string_lit->getString().str();
        else
            group.clear();

        std::string group_int;
        if (group_int_lit)
        {
#if LLVM_VERSION_MAJOR >= 13
            group_int = llvm::toString(group_int_lit->getValue(), /*radix*/ 10, /*signed*/ false);
#else
            group_int = group_int_lit->getValue().toString(/*radix*/ 10, /*signed*/ false);
#endif
        }
        else
        {
            const auto* group_int_expr = Result.Nodes.getNodeAs<clang::Expr>("group_int_arg");
            // a group without a numeric (the default argument) has none to record
            if (group_int_expr && llvm::isa<clang::CXXDefaultArgExpr>(group_int_expr))
                group_int_expr = nullptr;
            clang::Expr::EvalResult numeric;
            // a constant (e.g. GOBY_DEFINE_INTERVEHICLE_GROUP's static_cast) as its value, so it
            // matches the numerics bound to dynamic groups
            if (group_int_expr && !group_int_expr->isValueDependent() &&
                group_int_expr->EvaluateAsInt(numeric, *Result.Context))
            {
                auto value = numeric.Val.getInt().getZExtValue();
                if (value != std::numeric_limits<std::uint32_t>::max())
                    group_int = std::to_string(value);
            }
            else if (group_int_expr)
            {
                llvm::raw_string_ostream os(group_int);
                group_int_expr->printPretty(os, NULL, clang::PrintingPolicy(clang::LangOptions()));
                os.flush();
                boost::erase_all(group_int, " ");
            }
        }

        if (!dynamic_group_expr && !group_int.empty())
        {
            if (!group.empty())
                group += ";";
            group += group_int;
        }

        std::string type = "unknown";
        if (type_arg)
            type = as_string(*type_arg->getAsType());

        std::string scheme = "unknown";
        if (scheme_arg)
        {
            const int scheme_num = scheme_arg->getAsIntegral().getExtValue();
            scheme = goby::middleware::MarshallingScheme::to_string(scheme_num);
        }

        auto necessity = goby::middleware::Necessity::OPTIONAL;
        if (necessity_arg)
            necessity = static_cast<goby::middleware::Necessity>(
                necessity_arg->getAsIntegral().getExtValue());

        auto method = pubsub_call_expr->getMethodDecl()->getNameAsString();
        bool is_regex = (method.find("regex") != std::string::npos);

        auto direction = (method.find("publish") != std::string::npos)
                             ? PubSubEntry::Direction::PUBLISH
                             : PubSubEntry::Direction::SUBSCRIBE;

        std::set<std::string> internal_groups{
            "goby::middleware::interprocess::to_portal",
            "goby::middleware::interprocess::from_portal",
            "goby::middleware::interprocess::regex",
            "goby::middleware::SerializationUnSubscribeAll",
            "goby::middleware::Thread::joinable",
            "goby::middleware::Thread::shutdown",
            "goby::middleware::intervehicle::modem_data_in",
            "goby::middleware::intervehicle::modem_data_out",
            "goby::middleware::intervehicle::metadata_request",
            "goby::middleware::intervehicle::modem_ack_in",
            "goby::middleware::intervehicle::modem_expire_in",
            "goby::middleware::intervehicle::modem_subscription_forward_tx"};

        // TODO: fix generation of internal groups (all show as "unknown")
        // until then, hide them
        if (internal_groups.count(group))
            return;

        PubSubEntry entry(layer, direction, thread, group, scheme, type, thread_is_known, necessity,
                          is_regex);
        if (dynamic_group_expr)
        {
            entry.dynamic = true;
            entry.numeric_runtime = dynamic.numeric_runtime;
            if (group == "*")
                entry.expr = dynamic.expr;
            if (!dynamic.pointer.empty())
            {
                pending_.emplace_back(entry, dynamic.pointer);
                return;
            }
        }
        entries_.insert(entry);
    }

    /// \brief Resolve groups held behind smart pointers, once every translation unit is read
    void finalize(const DynamicGroupResets& resets)
    {
        for (auto& pending : pending_)
        {
            auto entry = pending.first;
            if (const auto* info = resets.find(pending.second))
            {
                entry.group = info->group;
                entry.numeric_runtime = info->numeric_runtime;
                entry.expr = info->group == "*" ? info->expr : "";
            }
            entries_.insert(entry);
        }
        pending_.clear();
    }

    const std::set<PubSubEntry>& entries() const { return entries_; }
    const std::set<std::string>& bases(const std::string& thread) { return bases_[thread]; }
    const std::set<std::string>& parents(const std::string& thread) { return parents_[thread]; }

  private:
    std::string as_string(const clang::Type& type)
    {
        // todo: see if there's a cleaner way to get this with the template parameters
        std::string str(type.getCanonicalTypeInternal().getAsString());

        // remove class, struct
        std::string class_str = "class ";
        if (str.find(class_str) == 0)
            str = str.substr(class_str.size());

        std::string struct_str = "struct ";
        if (str.find(struct_str) == 0)
            str = str.substr(struct_str.size());

        return str;
    }

    std::string as_string(const clang::CXXRecordDecl& cxx_decl)
    {
        return as_string(*(cxx_decl.getTypeForDecl()));
    }

    // recursively insert all base classes
    void insert_bases(std::set<std::string>& bases, const clang::CXXRecordDecl* thread_decl)
    {
        std::string thread = as_string(*thread_decl);
        for (auto it = thread_decl->bases_begin(), end = thread_decl->bases_end(); it != end; ++it)
        {
            bases.insert(as_string(*(it->getType()->getAsCXXRecordDecl())));
            insert_bases(bases, it->getType()->getAsCXXRecordDecl());
        }
    }

  private:
    std::set<PubSubEntry> entries_;
    // dynamic entries whose group is behind a smart pointer, with the pointer's qualified name
    std::vector<std::pair<PubSubEntry, std::string>> pending_;
    // map thread to bases
    std::map<std::string, std::set<std::string>> bases_;
    std::map<std::string, std::set<std::string>> parents_;
};

int goby::clang::generate(::clang::tooling::ClangTool& Tool, std::string output_directory,
                          std::string output_file, std::string target_name)
{
    PubSubAggregator publish_aggregator, subscribe_aggregator;
    ::clang::ast_matchers::MatchFinder finder;

    finder.addMatcher(pubsub_matcher("publish"), &publish_aggregator);
    finder.addMatcher(pubsub_matcher("subscribe"), &subscribe_aggregator);
    finder.addMatcher(pubsub_matcher("subscribe_type_regex"), &subscribe_aggregator);
    finder.addMatcher(pubsub_dynamic_matcher("publish_dynamic"), &publish_aggregator);
    finder.addMatcher(pubsub_dynamic_matcher("subscribe_dynamic"), &subscribe_aggregator);
    DynamicGroupResets dynamic_group_resets;
    finder.addMatcher(dynamic_group_reset_matcher(), &dynamic_group_resets);

    if (output_file.empty())
        output_file = target_name + "_interface.yml";

    std::string file_name(output_directory + "/" + output_file);
    std::ofstream ofs(file_name.c_str());
    if (!ofs.is_open())
    {
        std::cerr << "Failed to open " << file_name << " for writing" << std::endl;
        exit(EXIT_FAILURE);
    }

    auto retval = Tool.run(::clang::tooling::newFrontendActionFactory(&finder).get());
    publish_aggregator.finalize(dynamic_group_resets);
    subscribe_aggregator.finalize(dynamic_group_resets);

    std::set<Layer> layers_in_use;
    std::set<std::pair<std::string, bool>> threads_in_use;
    for (const auto& e : publish_aggregator.entries())
    {
        layers_in_use.insert(e.layer);

        // only include most derived thread class
        if (publish_aggregator.parents(e.thread).empty() &&
            subscribe_aggregator.parents(e.thread).empty())
            threads_in_use.insert(std::make_pair(e.thread, e.thread_is_known));
    }
    for (const auto& e : subscribe_aggregator.entries())
    {
        layers_in_use.insert(e.layer);
        if (publish_aggregator.parents(e.thread).empty() &&
            subscribe_aggregator.parents(e.thread).empty())
            threads_in_use.insert(std::make_pair(e.thread, e.thread_is_known));
    }

    // intervehicle requires interprocess at this point
    if (layers_in_use.count(Layer::INTERVEHICLE))
        layers_in_use.insert(Layer::INTERPROCESS);

    // add interthread so that we can get bases even if there's no interthread pubsub actually happening
    if (layers_in_use.count(Layer::INTERPROCESS))
        layers_in_use.insert(Layer::INTERTHREAD);

    YAML::Emitter yaml_out;
    {
        goby::yaml::YMap root_map(yaml_out);
        root_map.add("application", std::string(target_name));

        // put inner most layer last
        for (auto layer_it = layers_in_use.rbegin(), end = layers_in_use.rend(); layer_it != end;
             ++layer_it)
        {
            Layer layer = *layer_it;
            root_map.add_key(goby::middleware::tool::interface::layer_name(layer));
            goby::yaml::YMap layer_map(yaml_out);

            auto find_most_derived_parents = [&](std::string thread) -> std::set<std::string>
            {
                // if e.thread has parents, use them instead
                std::set<std::string> most_derived{thread};

                bool parents_found = true;
                while (parents_found)
                {
                    std::set<std::string> new_most_derived;
                    // assume we find no parents until we do
                    parents_found = false;
                    for (const auto& thread : most_derived)
                    {
                        auto pub_parents = publish_aggregator.parents(thread);
                        auto sub_parents = subscribe_aggregator.parents(thread);
                        if (!(pub_parents.empty() && sub_parents.empty()))
                        {
                            parents_found = true;
                            new_most_derived.insert(pub_parents.begin(), pub_parents.end());
                            new_most_derived.insert(sub_parents.begin(), sub_parents.end());
                        }
                        else
                        {
                            new_most_derived.insert(thread);
                        }
                    }
                    most_derived = new_most_derived;
                }
                return most_derived;
            };

            auto emit_pub_sub =
                [&](goby::yaml::YMap& map, const std::set<std::string>& thread_and_bases)
            {
                {
                    map.add_key("publishes");
                    goby::yaml::YSeq publish_seq(yaml_out);
                    for (auto e : publish_aggregator.entries())
                    {
                        auto most_derived = find_most_derived_parents(e.thread);

                        // show inner publications
                        if (e.layer >= layer &&
                            (layer != Layer::INTERTHREAD || thread_and_bases.count(e.thread)))
                        {
                            for (const auto& thread : most_derived)
                            {
                                // overwrite with most derived
                                e.thread = thread;

                                e.write_yaml_map(yaml_out, layer != Layer::INTERTHREAD,
                                                 e.layer > layer, false);

                                // special case: Intervehicle publishes both PROTOBUF and DCCL version on inner layers
                                if (e.layer > layer && e.layer == Layer::INTERVEHICLE &&
                                    e.scheme == "DCCL")
                                {
                                    auto pb_e = e;
                                    pb_e.scheme = "PROTOBUF";
                                    pb_e.write_yaml_map(yaml_out, layer != Layer::INTERTHREAD,
                                                        pb_e.layer > layer, false);
                                }
                            }
                        }
                    }
                }

                {
                    map.add_key("subscribes");
                    goby::yaml::YSeq subscribe_seq(yaml_out);
                    for (auto e : subscribe_aggregator.entries())
                    {
                        if (e.layer == layer &&
                            (layer != Layer::INTERTHREAD || thread_and_bases.count(e.thread)))
                        {
                            auto most_derived = find_most_derived_parents(e.thread);
                            for (const auto& thread : most_derived)
                            {
                                // overwrite with most derived
                                e.thread = thread;
                                e.write_yaml_map(yaml_out, layer != Layer::INTERTHREAD);
                            }
                        }
                    }
                }
            };

            if (layer == Layer::INTERTHREAD)
            {
                layer_map.add_key("threads");
                goby::yaml::YSeq thread_seq(yaml_out);
                for (const auto& thread : threads_in_use)
                {
                    goby::yaml::YMap thread_map(yaml_out);
                    {
                        thread_map.add("name", thread.first);
                        thread_map.add("known", thread.second);
                        std::set<std::string> thread_and_bases{thread.first};

                        std::set<std::string> bases;
                        std::function<void(std::string)> add_bases_recurse = [&](std::string thread)
                        {
                            std::set<std::string> new_bases;
                            const auto& pub_bases = publish_aggregator.bases(thread);
                            const auto& sub_bases = subscribe_aggregator.bases(thread);
                            new_bases.insert(pub_bases.begin(), pub_bases.end());
                            new_bases.insert(sub_bases.begin(), sub_bases.end());
                            bases.insert(new_bases.begin(), new_bases.end());
                            for (const auto& base : new_bases) add_bases_recurse(base);
                        };

                        add_bases_recurse(thread.first);

                        if (!bases.empty())
                        {
                            thread_map.add_key("bases");
                            goby::yaml::YSeq bases_seq(yaml_out);

                            for (const auto& base : bases)
                            {
                                bases_seq.add(base);
                                thread_and_bases.insert(base);
                            }
                        }
                        emit_pub_sub(thread_map, thread_and_bases);
                    }
                }
            }
            else
            {
                emit_pub_sub(layer_map, {});
            }
        }
    }

    ofs << yaml_out.c_str();

    return retval;
}
