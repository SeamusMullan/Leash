// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "leash/syntax/ast.hpp"
#include "leash/syntax/parser.hpp"

namespace {

using leash::syntax::FileId;
using leash::syntax::NodeId;
using leash::syntax::Parsed;
using namespace leash::syntax::ast;

[[nodiscard]] Parsed parse(std::string_view source) {
    return leash::syntax::parse_program(source, FileId{1});
}

[[nodiscard]] const Program& program_of(const Parsed& parsed) {
    if (!parsed.program.has_value()) {
        throw std::runtime_error("parse produced no program");
    }
    return *parsed.program;
}

[[nodiscard]] const Path& module_of(const Program& program) {
    if (!program.module_path.has_value()) {
        throw std::runtime_error("program has no module");
    }
    return *program.module_path;
}

[[nodiscard]] const TypeExpr& type_of(const TypeDecl& decl) {
    if (!decl.type) {
        throw std::runtime_error("type declaration has no type");
    }
    return *decl.type;
}

[[nodiscard]] const Expr& value_of(const FieldInit& field) {
    if (!field.value) {
        throw std::runtime_error("field has no value");
    }
    return **field.value;
}

[[nodiscard]] const Expr& expr_of(const std::optional<ExprPtr>& value) {
    if (!value.has_value()) {
        throw std::runtime_error("expected an expression");
    }
    return **value;
}

template <typename T> [[nodiscard]] const T* item(const Parsed& parsed, std::size_t index) {
    if (!parsed.program.has_value() || index >= program_of(parsed).items.size()) {
        return nullptr;
    }
    return std::get_if<T>(&program_of(parsed).items.at(index));
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

[[nodiscard]] const Expr* first_let_value(const Parsed& parsed) {
    const auto* flow = item<FlowDecl>(parsed, program_of(parsed).items.size() - 1);
    if (flow == nullptr || flow->body.stmts.empty()) {
        return nullptr;
    }
    const auto* let = std::get_if<LetStmt>(&flow->body.stmts.front());
    return let == nullptr ? nullptr : let->value.get();
}

} // namespace

TEST_CASE("parser reads module and use declarations", "[syntax][parser]") {
    const std::string source = "module app.main\n\nuse std.drivers\nuse std.repo.{Repo, Patch}\n";
    const Parsed parsed = parse(source);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.program.has_value());
    const Program& program = program_of(parsed);
    REQUIRE(program.module_path.has_value());
    CHECK(module_of(program).segments.size() == 2);
    CHECK(module_of(program).segments.at(0).text == "app");

    REQUIRE(program.items.size() == 2);
    const auto* drivers = std::get_if<UseDecl>(&program.items.at(0));
    REQUIRE(drivers != nullptr);
    CHECK(drivers->path.segments.size() == 2);
    CHECK_FALSE(drivers->grouped);

    const auto* repo = std::get_if<UseDecl>(&program.items.at(1));
    REQUIRE(repo != nullptr);
    CHECK(repo->grouped);
    REQUIRE(repo->names.size() == 2);
    CHECK(repo->names.at(0).text == "Repo");
    CHECK(repo->names.at(1).text == "Patch");
}

TEST_CASE("parser applies operator precedence with a Pratt loop", "[syntax][parser]") {
    const std::string source = "let x = 1 + 2 * 3 == 7 and not false or true\n";
    const Parsed parsed = parse(source);
    REQUIRE(parsed.ok());
    const auto* constant = item<ConstDecl>(parsed, 0);
    REQUIRE(constant != nullptr);
    const auto* or_expr = std::get_if<Binary>(&constant->value->kind);
    REQUIRE(or_expr != nullptr);
    CHECK(or_expr->op == BinaryOp::Or);

    const auto* and_expr = std::get_if<Binary>(&or_expr->lhs->kind);
    REQUIRE(and_expr != nullptr);
    CHECK(and_expr->op == BinaryOp::And);

    const auto* comparison = std::get_if<Binary>(&and_expr->lhs->kind);
    REQUIRE(comparison != nullptr);
    CHECK(comparison->op == BinaryOp::Eq);

    const auto* sum = std::get_if<Binary>(&comparison->lhs->kind);
    REQUIRE(sum != nullptr);
    CHECK(sum->op == BinaryOp::Add);
    const auto* product = std::get_if<Binary>(&sum->rhs->kind);
    REQUIRE(product != nullptr);
    CHECK(product->op == BinaryOp::Mul);

    const auto* negation = std::get_if<Unary>(&and_expr->rhs->kind);
    REQUIRE(negation != nullptr);
    CHECK(negation->op == UnaryOp::Not);
}

TEST_CASE("parser honours the newline statement-termination rule", "[syntax][parser]") {
    SECTION("a newline ends a statement") {
        const std::string source = "flow f(input) {\n  let a = 1\n  let b = 2\n}\n";
        const Parsed parsed = parse(source);
        REQUIRE(parsed.ok());
        const auto* flow = item<FlowDecl>(parsed, 0);
        REQUIRE(flow != nullptr);
        CHECK(flow->body.stmts.size() == 2);
    }

    SECTION("a binary operator on the next line continues the expression") {
        const std::string source = "flow f(input) {\n  let a = 1 +\n    2\n}\n";
        const Parsed parsed = parse(source);
        REQUIRE(parsed.ok());
        const auto* flow = item<FlowDecl>(parsed, 0);
        REQUIRE(flow != nullptr);
        REQUIRE(flow->body.stmts.size() == 1);
        const auto* let = std::get_if<LetStmt>(&flow->body.stmts.front());
        REQUIRE(let != nullptr);
        CHECK(std::holds_alternative<Binary>(let->value->kind));
    }

    SECTION("a dot on the next line continues a postfix chain") {
        const std::string source = "flow f(input) {\n  let a = x\n    .foo()\n}\n";
        const Parsed parsed = parse(source);
        REQUIRE(parsed.ok());
        const auto* flow = item<FlowDecl>(parsed, 0);
        REQUIRE(flow != nullptr);
        REQUIRE(flow->body.stmts.size() == 1);
        const auto* let = std::get_if<LetStmt>(&flow->body.stmts.front());
        REQUIRE(let != nullptr);
        CHECK(std::holds_alternative<Method>(let->value->kind));
    }

    SECTION("a parenthesis on the next line does not continue a call") {
        const std::string source = "flow f(input) {\n  let a = x\n    (1)\n}\n";
        const Parsed parsed = parse(source);
        REQUIRE(parsed.ok());
        const auto* flow = item<FlowDecl>(parsed, 0);
        REQUIRE(flow != nullptr);
        CHECK(flow->body.stmts.size() == 2);
    }
}

TEST_CASE("parser builds record, list, variant and lambda nodes", "[syntax][parser]") {
    const std::string source = "flow f(input) {\n"
                               "  let a = Triage { id: 1, severity: .High }\n"
                               "  let b = [1, 2, 3]\n"
                               "  let c = items.map(x => x * 2)\n"
                               "  let d = map(items, concurrency: 8) { item =>\n"
                               "    item\n"
                               "  }\n"
                               "}\n";
    const Parsed parsed = parse(source);
    REQUIRE(parsed.ok());
    const auto* flow = item<FlowDecl>(parsed, 0);
    REQUIRE(flow != nullptr);
    REQUIRE(flow->body.stmts.size() == 4);

    const auto* first = std::get_if<LetStmt>(&flow->body.stmts.at(0));
    REQUIRE(first != nullptr);
    const auto* named = std::get_if<NamedRecord>(&first->value->kind);
    REQUIRE(named != nullptr);
    REQUIRE(named->record.fields.size() == 2);
    CHECK(named->record.fields.at(0).name.text == "id");
    REQUIRE(named->record.fields.at(1).value.has_value());
    CHECK(std::holds_alternative<Variant>(value_of(named->record.fields.at(1)).kind));

    const auto* second = std::get_if<LetStmt>(&flow->body.stmts.at(1));
    REQUIRE(second != nullptr);
    const auto* list = std::get_if<ListLit>(&second->value->kind);
    REQUIRE(list != nullptr);
    CHECK(list->elems.size() == 3);

    const auto* third = std::get_if<LetStmt>(&flow->body.stmts.at(2));
    REQUIRE(third != nullptr);
    const auto* method = std::get_if<Method>(&third->value->kind);
    REQUIRE(method != nullptr);
    CHECK(method->name.text == "map");
    REQUIRE(method->args.size() == 1);
    const auto* lambda = std::get_if<ExprPtr>(&method->args.at(0));
    REQUIRE(lambda != nullptr);
    CHECK(std::holds_alternative<Lambda>((*lambda)->kind));

    const auto* fourth = std::get_if<LetStmt>(&flow->body.stmts.at(3));
    REQUIRE(fourth != nullptr);
    const auto* call = std::get_if<Call>(&fourth->value->kind);
    REQUIRE(call != nullptr);
    REQUIRE(call->trailing.has_value());
    const auto* trailing = std::get_if<Lambda>(&expr_of(call->trailing).kind);
    REQUIRE(trailing != nullptr);
    CHECK(trailing->trailing);
    REQUIRE(trailing->params.size() == 1);
}

TEST_CASE("parser handles ask, fail and complete", "[syntax][parser]") {
    const std::string source = "flow f(input) {\n"
                               "  let a = ask helper(\"hello {input.topic}\", retries: 1)\n"
                               "  if len(a) == 0 { fail Model(\"empty\", a) }\n"
                               "  complete a\n"
                               "}\n";
    const Parsed parsed = parse(source);
    REQUIRE(parsed.ok());
    const auto* flow = item<FlowDecl>(parsed, 0);
    REQUIRE(flow != nullptr);
    REQUIRE(flow->body.stmts.size() == 3);

    const auto* let = std::get_if<LetStmt>(&flow->body.stmts.at(0));
    REQUIRE(let != nullptr);
    const auto* ask = std::get_if<Ask>(&let->value->kind);
    REQUIRE(ask != nullptr);
    REQUIRE(ask->args.size() == 2);

    const auto* if_stmt = std::get_if<ExprStmt>(&flow->body.stmts.at(1));
    REQUIRE(if_stmt != nullptr);
    const auto* if_expr = std::get_if<IfExpr>(&if_stmt->expr->kind);
    REQUIRE(if_expr != nullptr);
    REQUIRE(if_expr->then_block.stmts.size() == 1);
    const auto* nested = std::get_if<ExprStmt>(&if_expr->then_block.stmts.at(0));
    REQUIRE(nested != nullptr);
    const auto* fail = std::get_if<FailExpr>(&nested->expr->kind);
    REQUIRE(fail != nullptr);
    CHECK(fail->kind.text == "Model");
    CHECK(fail->args.size() == 2);

    const auto* complete = std::get_if<ExprStmt>(&flow->body.stmts.at(2));
    REQUIRE(complete != nullptr);
    CHECK(std::holds_alternative<Complete>(complete->expr->kind));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("every AST node has a unique id and a span inside the file", "[syntax][parser]") {
    const std::string source = "type T = { a: Int, b: [String] }\n"
                               "agent a = Agent { x: 1 }\n"
                               "flow f(input) -> T {\n"
                               "  let x = [1, 2]\n"
                               "  complete x\n"
                               "}\n";
    const Parsed parsed = parse(source);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.program.has_value());

    std::vector<NodeId> ids;
    std::function<void(const Expr&)> visit_expr =
        [&](const Expr& expr) { // NOLINT(readability-function-cognitive-complexity)
            ids.push_back(expr.id);
            CHECK(expr.span.start <= expr.span.end);
            CHECK(expr.span.end <= source.size());
            std::visit(
                [&](const auto& node) { // NOLINT(readability-function-cognitive-complexity)
                    using T = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<T, RecordLit>) {
                        for (const FieldInit& field : node.fields) {
                            if (field.value) {
                                visit_expr(**field.value);
                            }
                        }
                    } else if constexpr (std::is_same_v<T, NamedRecord>) {
                        for (const FieldInit& field : node.record.fields) {
                            if (field.value) {
                                visit_expr(**field.value);
                            }
                        }
                    } else if constexpr (std::is_same_v<T, ListLit>) {
                        for (const ExprPtr& element : node.elems) {
                            visit_expr(*element);
                        }
                    } else if constexpr (std::is_same_v<T, IfExpr>) {
                        visit_expr(*node.cond);
                        for (const Stmt& stmt : node.then_block.stmts) {
                            if (const auto* statement = std::get_if<ExprStmt>(&stmt)) {
                                visit_expr(*statement->expr);
                            }
                        }
                    } else if constexpr (std::is_same_v<T, Unary>) {
                        visit_expr(*node.operand);
                    } else if constexpr (std::is_same_v<T, Binary>) {
                        visit_expr(*node.lhs);
                        visit_expr(*node.rhs);
                    } else if constexpr (std::is_same_v<T, Call>) {
                        visit_expr(*node.callee);
                    } else if constexpr (std::is_same_v<T, Method> || std::is_same_v<T, Field>) {
                        visit_expr(*node.receiver);
                    } else if constexpr (std::is_same_v<T, Lambda>) {
                        visit_expr(*node.body);
                    } else if constexpr (std::is_same_v<T, Ask>) {
                        visit_expr(*node.target);
                    } else if constexpr (std::is_same_v<T, Complete>) {
                        visit_expr(*node.value);
                    } else if constexpr (std::is_same_v<T, Interp>) {
                        for (const InterpPart& part : node.parts) {
                            if (const auto* sub = std::get_if<ExprPtr>(&part)) {
                                visit_expr(**sub);
                            }
                        }
                    }
                },
                expr.kind);
        };

    for (const Item& node : program_of(parsed).items) {
        ids.push_back(node_id(node));
        if (const auto* agent = std::get_if<AgentDecl>(&node)) {
            visit_expr(*agent->value);
        }
        if (const auto* flow = std::get_if<FlowDecl>(&node)) {
            for (const Stmt& stmt : flow->body.stmts) {
                ids.push_back(node_id(stmt));
                if (const auto* let = std::get_if<LetStmt>(&stmt)) {
                    visit_expr(*let->value);
                } else if (const auto* expr = std::get_if<ExprStmt>(&stmt)) {
                    visit_expr(*expr->expr);
                }
            }
        }
    }

    ids.push_back(node_id(program_of(parsed)));
    for (std::size_t index = 0; index < ids.size(); ++index) {
        for (std::size_t other = index + 1; other < ids.size(); ++other) {
            CHECK(ids.at(index) != ids.at(other));
        }
    }
}

TEST_CASE("parser reports unsupported constructs with E0100", "[syntax][parser]") {
    const std::string source = "module m\n\nenum Severity { Low, High }\n";
    const Parsed parsed = parse(source);
    REQUIRE_FALSE(parsed.ok());
    REQUIRE_FALSE(parsed.diagnostics.empty());
    CHECK(parsed.diagnostics.front().code == "E0100");
    CHECK(parsed.diagnostics.front().message.find("enum") != std::string::npos);
    CHECK(parsed.diagnostics.front().message.find("WP-07") != std::string::npos);
}

TEST_CASE("parser recovers and reports two independent errors", "[syntax][parser]") {
    const std::string source = "flow a(input) {\n"
                               "  let x = 1 +\n"
                               "}\n"
                               "flow b(input) {\n"
                               "  let y = 2 +\n"
                               "}\n";
    const Parsed parsed = parse(source);
    REQUIRE(parsed.program.has_value());
    std::size_t expression_errors = 0;
    for (const auto& diagnostic : parsed.diagnostics) {
        if (diagnostic.code == "E0105") {
            ++expression_errors;
        }
    }
    CHECK(expression_errors == 2);
}

TEST_CASE("parser parses every example", "[syntax][parser][examples]") {
    const std::filesystem::path directory = std::filesystem::path{LEASH_SOURCE_DIR} / "examples";
    REQUIRE(std::filesystem::exists(directory));

    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".leash") {
            continue;
        }
        ++count;
        const std::string source = read_file(entry.path());
        INFO("file: " << entry.path().filename().string());
        const Parsed parsed = parse(source);
        REQUIRE(parsed.program.has_value());
        CHECK(program_of(parsed).span.end <= source.size());
    }
    CHECK(count >= 5);
}

TEST_CASE("the hello example parses without diagnostics", "[syntax][parser][examples]") {
    const std::string source =
        read_file(std::filesystem::path{LEASH_SOURCE_DIR} / "examples" / "01_hello.leash");
    const Parsed parsed = parse(source);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.program.has_value());
    REQUIRE(program_of(parsed).module_path.has_value());

    const auto* agent = item<AgentDecl>(parsed, 1);
    REQUIRE(agent != nullptr);
    CHECK(agent->name.text == "namer");
    REQUIRE(std::holds_alternative<NamedRecord>(agent->value->kind));

    const auto* type = item<TypeDecl>(parsed, 2);
    REQUIRE(type != nullptr);
    CHECK(type->name.text == "Suggestion");
    const auto* record = std::get_if<RecordType>(&type->type->kind);
    REQUIRE(record != nullptr);
    REQUIRE(record->fields.size() == 2);
    CHECK(record->fields.at(0).name.text == "name");
    CHECK(record->fields.at(0).refine.has_value());

    const auto* task = item<TaskDecl>(parsed, 3);
    REQUIRE(task != nullptr);
    CHECK(task->sections.size() == 3);

    const auto* flow = item<FlowDecl>(parsed, 4);
    REQUIRE(flow != nullptr);
    CHECK(flow->name.text == "main");
    REQUIRE(flow->body.stmts.size() >= 3);
}

TEST_CASE("example 02 first reports its unsupported task section", "[syntax][parser][examples]") {
    const std::string source =
        read_file(std::filesystem::path{LEASH_SOURCE_DIR} / "examples" / "02_fix_bug.leash");
    const Parsed parsed = parse(source);
    REQUIRE_FALSE(parsed.diagnostics.empty());
    CHECK(parsed.diagnostics.front().code == "E0100");
    CHECK(parsed.diagnostics.front().message.find("require") != std::string::npos);
}

TEST_CASE("example 03 reports enum, 05 reports tool, 04 is inside the v0 subset",
          "[syntax][parser][examples]") {
    const std::string triage = read_file(std::filesystem::path{LEASH_SOURCE_DIR} / "examples" /
                                         "03_triage_parallel.leash");
    const Parsed triage_parsed = parse(triage);
    REQUIRE_FALSE(triage_parsed.diagnostics.empty());
    CHECK(triage_parsed.diagnostics.front().code == "E0100");
    CHECK(triage_parsed.diagnostics.front().message.find("enum") != std::string::npos);

    const std::string mcp =
        read_file(std::filesystem::path{LEASH_SOURCE_DIR} / "examples" / "05_mcp_tools.leash");
    const Parsed mcp_parsed = parse(mcp);
    REQUIRE_FALSE(mcp_parsed.diagnostics.empty());
    CHECK(mcp_parsed.diagnostics.front().code == "E0100");
    CHECK(mcp_parsed.diagnostics.front().message.find("tool") != std::string::npos);

    const std::string debate =
        read_file(std::filesystem::path{LEASH_SOURCE_DIR} / "examples" / "04_review_debate.leash");
    const Parsed debate_parsed = parse(debate);
    CHECK(debate_parsed.ok());
}
