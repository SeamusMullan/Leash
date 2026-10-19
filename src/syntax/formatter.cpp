// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The Leash Authors

#include "leash/syntax/formatter.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "leash/syntax/ast.hpp"

namespace leash::syntax {
namespace {

constexpr int kIndentWidth = 2;
constexpr std::size_t kMaxWidth = 100;

[[nodiscard]] std::string path_text(const ast::Path& path) {
    std::string result;
    for (std::size_t index = 0; index < path.segments.size(); ++index) {
        if (index != 0) {
            result.push_back('.');
        }
        result.append(path.segments.at(index).text);
    }
    return result;
}

[[nodiscard]] std::string join(const std::vector<std::string>& parts, std::string_view separator) {
    std::string result;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index != 0) {
            result.append(separator);
        }
        result.append(parts.at(index));
    }
    return result;
}

[[nodiscard]] int binary_precedence(ast::BinaryOp operation) {
    switch (operation) {
    case ast::BinaryOp::Or:
        return 1;
    case ast::BinaryOp::And:
        return 2;
    case ast::BinaryOp::Eq:
    case ast::BinaryOp::Ne:
    case ast::BinaryOp::Lt:
    case ast::BinaryOp::Le:
    case ast::BinaryOp::Gt:
    case ast::BinaryOp::Ge:
        return 4;
    case ast::BinaryOp::Add:
    case ast::BinaryOp::Sub:
        return 5;
    case ast::BinaryOp::Mul:
    case ast::BinaryOp::Div:
    case ast::BinaryOp::Rem:
        return 6;
    }
    return 0;
}

[[nodiscard]] std::string_view binary_spelling(ast::BinaryOp operation) {
    switch (operation) {
    case ast::BinaryOp::Or:
        return "or";
    case ast::BinaryOp::And:
        return "and";
    case ast::BinaryOp::Eq:
        return "==";
    case ast::BinaryOp::Ne:
        return "!=";
    case ast::BinaryOp::Lt:
        return "<";
    case ast::BinaryOp::Le:
        return "<=";
    case ast::BinaryOp::Gt:
        return ">";
    case ast::BinaryOp::Ge:
        return ">=";
    case ast::BinaryOp::Add:
        return "+";
    case ast::BinaryOp::Sub:
        return "-";
    case ast::BinaryOp::Mul:
        return "*";
    case ast::BinaryOp::Div:
        return "/";
    case ast::BinaryOp::Rem:
        return "%";
    }
    return "?";
}

class Printer {
  public:
    std::string run(const ast::Program& program) {
        write_leading(program.leading);

        if (program.module_path.has_value()) {
            pad();
            write("module ");
            write(path_text(*program.module_path));
            newline();
            if (!program.items.empty()) {
                blank_line();
            }
        }

        for (std::size_t index = 0; index < program.items.size(); ++index) {
            const ast::Item& item = program.items.at(index);
            const bool both_use =
                index > 0 && std::holds_alternative<ast::UseDecl>(program.items.at(index - 1)) &&
                std::holds_alternative<ast::UseDecl>(item);
            if (index > 0 && !both_use) {
                blank_line();
            }
            write_leading(std::visit(
                [](const auto& node) -> const ast::Leading& { return node.leading; }, item));
            pad();
            write_item(item);
            newline();
        }

        if (!program.trailing.comments.empty()) {
            blank_line();
            write_leading(program.trailing);
        }

        while (!out_.empty() && out_.back() == '\n') {
            out_.pop_back();
        }
        if (!out_.empty()) {
            out_.push_back('\n');
        }
        return out_;
    }

  private:
    // ------------------------------------------------------------- primitives

    void write(std::string_view text) {
        out_.append(text);
        for (char character : text) {
            if (character == '\n') {
                column_ = 0;
            } else {
                ++column_;
            }
        }
    }

    void newline() {
        out_.push_back('\n');
        column_ = 0;
    }

    void pad() {
        out_.append(static_cast<std::size_t>(indent_), ' ');
        column_ = static_cast<std::size_t>(indent_);
    }

    void blank_line() {
        if (out_.empty() || (out_.size() >= 2 && out_.ends_with("\n\n"))) {
            return;
        }
        out_.push_back('\n');
        column_ = 0;
    }

    [[nodiscard]] bool fits(std::string_view text) const {
        return !text.contains('\n') && column_ + text.size() <= kMaxWidth;
    }

    void write_comment(const ast::Comment& comment) {
        const std::string_view text = comment.text;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t end = text.find('\n', start);
            const std::string_view line = end == std::string_view::npos
                                              ? text.substr(start)
                                              : text.substr(start, end - start);
            pad();
            write(line);
            newline();
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
    }

    void write_leading(const ast::Leading& leading, bool allow_blank = true) {
        if (allow_blank && leading.blank_before) {
            blank_line();
        }
        for (const ast::Comment& comment : leading.comments) {
            write_comment(comment);
        }
    }

    // ------------------------------------------------------------------- items

    void write_item(const ast::Item& item) {
        if (const auto* use = std::get_if<ast::UseDecl>(&item)) {
            write_use(*use);
        } else if (const auto* constant = std::get_if<ast::ConstDecl>(&item)) {
            write_const(*constant);
        } else if (const auto* type = std::get_if<ast::TypeDecl>(&item)) {
            write_type_decl(*type);
        } else if (const auto* function = std::get_if<ast::FnDecl>(&item)) {
            write_fn(*function);
        } else if (const auto* agent = std::get_if<ast::AgentDecl>(&item)) {
            write_agent(*agent);
        } else if (const auto* task = std::get_if<ast::TaskDecl>(&item)) {
            write_task(*task);
        } else if (const auto* flow = std::get_if<ast::FlowDecl>(&item)) {
            write_flow(*flow);
        }
    }

    void write_use(const ast::UseDecl& use) {
        write("use ");
        write(path_text(use.path));
        if (use.grouped) {
            write(".{");
            std::vector<std::string> names;
            names.reserve(use.names.size());
            for (const ast::Ident& name : use.names) {
                names.emplace_back(name.text);
            }
            write(join(names, ", "));
            write("}");
        }
    }

    void write_const(const ast::ConstDecl& constant) {
        if (constant.is_pub) {
            write("pub ");
        }
        write("let ");
        write(constant.name.text);
        if (constant.type) {
            write(": ");
            write(type_flat(*constant.type));
        }
        write(" = ");
        write_expr(*constant.value);
    }

    void write_type_decl(const ast::TypeDecl& type) {
        if (type.is_pub) {
            write("pub ");
        }
        write("type ");
        write(type.name.text);
        write(" = ");
        if (type.type) {
            write_type(*type.type);
        }
    }

    void write_fn(const ast::FnDecl& function) {
        if (function.is_pub) {
            write("pub ");
        }
        write("fn ");
        write(function.name.text);
        write("(");
        write_params(function.params);
        write(")");
        if (function.ret) {
            write(" -> ");
            write(type_flat(*function.ret));
        }
        write_effects(function.effects);
        write(" ");
        if (function.body.stmts.empty() && !function.body.tail.has_value() &&
            function.body.trailing.comments.empty()) {
            write("{}");
        } else {
            write_block(function.body);
        }
    }

    void write_params(const std::vector<ast::Param>& params) {
        std::vector<std::string> parts;
        parts.reserve(params.size());
        for (const ast::Param& param : params) {
            std::string part{param.name.text};
            if (param.type) {
                part += ": ";
                part += type_flat(*param.type);
            }
            if (param.default_value) {
                part += " = ";
                part += expr_flat(*param.default_value);
            }
            parts.push_back(std::move(part));
        }
        write(join(parts, ", "));
    }

    void write_effects(const std::optional<ast::EffectClause>& effects) {
        if (!effects.has_value()) {
            return;
        }
        write(" ! {");
        std::vector<std::string> names;
        names.reserve(effects->effects.size());
        for (const ast::Path& effect : effects->effects) {
            names.push_back(path_text(effect));
        }
        write(join(names, ", "));
        write("}");
    }

    void write_agent(const ast::AgentDecl& agent) {
        if (agent.is_pub) {
            write("pub ");
        }
        write("agent ");
        write(agent.name.text);
        write(" = ");
        write_expr(*agent.value);
    }

    void write_task(const ast::TaskDecl& task) {
        if (task.is_pub) {
            write("pub ");
        }
        write("task ");
        write(task.name.text);
        write(" {");
        newline();
        indent_ += kIndentWidth;

        for (const ast::TaskSection& section : task.sections) {
            const ast::Leading& leading = std::visit(
                [](const auto& node) -> const ast::Leading& { return node.leading; }, section);
            write_leading(leading);
            pad();
            if (const auto* input = std::get_if<ast::TaskInput>(&section)) {
                write("input ");
                write_record_type(input->type);
            } else if (const auto* output = std::get_if<ast::TaskOutput>(&section)) {
                write("output ");
                if (output->type) {
                    write(type_flat(*output->type));
                }
            } else if (const auto* budget = std::get_if<ast::TaskBudget>(&section)) {
                write("budget ");
                write_expr(*budget->value);
            }
            newline();
        }

        indent_ -= kIndentWidth;
        pad();
        write("}");
    }

    void write_flow(const ast::FlowDecl& flow) {
        if (flow.is_pub) {
            write("pub ");
        }
        write("flow ");
        write(flow.name.text);
        write("(");
        write(flow.param.name.text);
        if (flow.param.type) {
            write(": ");
            write(type_flat(*flow.param.type));
        }
        write(")");
        if (flow.ret) {
            write(" -> ");
            write(type_flat(*flow.ret));
        }
        write_effects(flow.effects);
        write(" ");
        write_block(flow.body);
    }

    // ------------------------------------------------------------------ blocks

    void write_block(const ast::Block& block) {
        if (block.stmts.empty() && !block.tail.has_value() && block.trailing.comments.empty()) {
            write("{}");
            return;
        }
        write("{");
        newline();
        indent_ += kIndentWidth;
        write_block_contents(block);
        indent_ -= kIndentWidth;
        pad();
        write("}");
    }

    void write_block_contents(const ast::Block& block) {
        for (std::size_t index = 0; index < block.stmts.size(); ++index) {
            const ast::Stmt& stmt = block.stmts.at(index);
            const ast::Leading& leading = std::visit(
                [](const auto& node) -> const ast::Leading& { return node.leading; }, stmt);
            write_leading(leading, index != 0);
            pad();
            write_stmt(stmt);
            newline();
        }
        if (block.tail.has_value()) {
            pad();
            write_expr(**block.tail);
            newline();
        }
        for (const ast::Comment& comment : block.trailing.comments) {
            write_comment(comment);
        }
    }

    void write_stmt(const ast::Stmt& stmt) {
        if (const auto* let = std::get_if<ast::LetStmt>(&stmt)) {
            write("let ");
            write(let->name.text);
            if (let->type) {
                write(": ");
                write(type_flat(*let->type));
            }
            write(" = ");
            write_expr(*let->value);
        } else if (const auto* expr = std::get_if<ast::ExprStmt>(&stmt)) {
            write_expr(*expr->expr);
        }
    }

    // ------------------------------------------------------------- expressions

    void write_expr(const ast::Expr& expr) {
        const std::string flat = expr_flat(expr);

        if (std::holds_alternative<ast::Block>(expr.kind)) {
            write_block(std::get<ast::Block>(expr.kind));
            return;
        }
        if (std::holds_alternative<ast::IfExpr>(expr.kind)) {
            write_if(std::get<ast::IfExpr>(expr.kind));
            return;
        }
        if (const auto* lambda = std::get_if<ast::Lambda>(&expr.kind)) {
            write_lambda(*lambda);
            return;
        }
        if (const auto* named = std::get_if<ast::NamedRecord>(&expr.kind)) {
            if (fits(flat) && !has_comments(named->record)) {
                write(flat);
            } else {
                write(path_text(named->type));
                write(" ");
                write_record_lit(named->record, true);
            }
            return;
        }
        if (const auto* record = std::get_if<ast::RecordLit>(&expr.kind)) {
            write_record_lit(*record, !fits(flat));
            return;
        }
        if (const auto* list = std::get_if<ast::ListLit>(&expr.kind)) {
            write_list_lit(*list);
            return;
        }
        if (const auto* call = std::get_if<ast::Call>(&expr.kind)) {
            write_call(*call, flat);
            return;
        }
        if (const auto* method = std::get_if<ast::Method>(&expr.kind)) {
            write_method(*method, flat);
            return;
        }
        if (const auto* ask = std::get_if<ast::Ask>(&expr.kind)) {
            write_ask(*ask, flat);
            return;
        }
        if (const auto* fail = std::get_if<ast::FailExpr>(&expr.kind)) {
            write_fail(*fail, flat);
            return;
        }
        write(flat);
    }

    void write_if(const ast::IfExpr& expr) {
        write("if ");
        write(expr_flat(*expr.cond));
        write(" ");
        write_block(expr.then_block);
        if (expr.else_branch.has_value()) {
            write(" else ");
            const ast::Expr& other = **expr.else_branch;
            if (std::holds_alternative<ast::IfExpr>(other.kind)) {
                write_if(std::get<ast::IfExpr>(other.kind));
            } else if (std::holds_alternative<ast::Block>(other.kind)) {
                write_block(std::get<ast::Block>(other.kind));
            } else {
                write_expr(other);
            }
        }
    }

    void write_lambda(const ast::Lambda& lambda) {
        std::string params;
        if (lambda.params.size() == 1 && !lambda.parenthesised && !lambda.trailing) {
            params = std::string{lambda.params.front().text};
        } else {
            std::vector<std::string> names;
            names.reserve(lambda.params.size());
            for (const ast::Ident& param : lambda.params) {
                names.emplace_back(param.text);
            }
            params = "(" + join(names, ", ") + ")";
        }

        if (lambda.trailing) {
            write("{ ");
            write(params);
            write(" =>");
            if (const auto* block = std::get_if<ast::Block>(&lambda.body->kind)) {
                if (block->stmts.empty() && !block->tail.has_value() &&
                    block->trailing.comments.empty()) {
                    write(" }");
                    return;
                }
                newline();
                indent_ += kIndentWidth;
                write_block_contents(*block);
                indent_ -= kIndentWidth;
                pad();
                write("}");
            } else {
                write(" ");
                write_expr(*lambda.body);
                write(" }");
            }
            return;
        }

        write(params);
        write(" => ");
        if (std::holds_alternative<ast::Block>(lambda.body->kind)) {
            write_block(std::get<ast::Block>(lambda.body->kind));
        } else {
            write_expr(*lambda.body);
        }
    }

    static bool has_comments(const ast::RecordLit& record) {
        if (!record.trailing.comments.empty()) {
            return true;
        }
        return std::ranges::any_of(record.fields, [](const ast::FieldInit& field) {
            return !field.leading.comments.empty();
        });
    }

    static bool has_comments(const ast::RecordType& record) {
        if (!record.trailing.comments.empty()) {
            return true;
        }
        return std::ranges::any_of(record.fields, [](const ast::RecordField& field) {
            return !field.leading.comments.empty();
        });
    }

    void write_record_lit(const ast::RecordLit& record, bool force_multiline) {
        if (record.fields.empty() && record.trailing.comments.empty()) {
            write("{}");
            return;
        }
        if (!force_multiline && !has_comments(record)) {
            write(record_flat(record));
            return;
        }
        write("{");
        newline();
        indent_ += kIndentWidth;
        for (const ast::FieldInit& field : record.fields) {
            write_leading(field.leading);
            pad();
            write(field.name.text);
            if (field.value.has_value()) {
                write(": ");
                write_expr(**field.value);
            }
            write(",");
            newline();
        }
        for (const ast::Comment& comment : record.trailing.comments) {
            write_comment(comment);
        }
        indent_ -= kIndentWidth;
        pad();
        write("}");
    }

    void write_list_lit(const ast::ListLit& list) {
        const std::string flat = expr_flat_of_list(list);
        if (list.elems.empty() && list.trailing.comments.empty()) {
            write("[]");
            return;
        }
        if (fits(flat) && list.trailing.comments.empty()) {
            write(flat);
            return;
        }
        write("[");
        newline();
        indent_ += kIndentWidth;
        for (const ast::ExprPtr& element : list.elems) {
            pad();
            write_expr(*element);
            write(",");
            newline();
        }
        for (const ast::Comment& comment : list.trailing.comments) {
            write_comment(comment);
        }
        indent_ -= kIndentWidth;
        pad();
        write("]");
    }

    void write_call(const ast::Call& call, const std::string& flat) {
        if (fits(flat)) {
            write(flat);
            return;
        }
        write(expr_flat(*call.callee, 9));
        write("(");
        newline();
        indent_ += kIndentWidth;
        for (const ast::Arg& arg : call.args) {
            pad();
            write_arg(arg);
            write(",");
            newline();
        }
        indent_ -= kIndentWidth;
        pad();
        write(")");
        write_optional_trailing(call.trailing);
    }

    void write_method(const ast::Method& method, const std::string& flat) {
        if (fits(flat)) {
            write(flat);
            return;
        }
        write(expr_flat(*method.receiver, 9));
        write(".");
        write(method.name.text);
        write("(");
        newline();
        indent_ += kIndentWidth;
        for (const ast::Arg& arg : method.args) {
            pad();
            write_arg(arg);
            write(",");
            newline();
        }
        indent_ -= kIndentWidth;
        pad();
        write(")");
        write_optional_trailing(method.trailing);
    }

    void write_optional_trailing(const std::optional<ast::ExprPtr>& trailing) {
        if (!trailing.has_value()) {
            return;
        }
        write(" ");
        write_expr(**trailing);
    }

    void write_ask(const ast::Ask& ask, const std::string& flat) {
        if (fits(flat)) {
            write(flat);
            return;
        }
        write("ask ");
        write(expr_flat(*ask.target, 9));
        write("(");
        newline();
        indent_ += kIndentWidth;
        for (const ast::Arg& arg : ask.args) {
            pad();
            write_arg(arg);
            write(",");
            newline();
        }
        indent_ -= kIndentWidth;
        pad();
        write(")");
    }

    void write_fail(const ast::FailExpr& fail, const std::string& flat) {
        if (fits(flat)) {
            write(flat);
            return;
        }
        write("fail ");
        write(fail.kind.text);
        write("(");
        newline();
        indent_ += kIndentWidth;
        for (const ast::ExprPtr& arg : fail.args) {
            pad();
            write_expr(*arg);
            write(",");
            newline();
        }
        indent_ -= kIndentWidth;
        pad();
        write(")");
    }

    void write_arg(const ast::Arg& arg) {
        if (const auto* named = std::get_if<ast::NamedArg>(&arg)) {
            write(named->name.text);
            write(": ");
            write_expr(*named->value);
        } else {
            write_expr(*std::get<ast::ExprPtr>(arg));
        }
    }

    // ------------------------------------------------------------------- types

    void write_type(const ast::TypeExpr& type) {
        if (const auto* record = std::get_if<ast::RecordType>(&type.kind)) {
            write_record_type(*record);
            return;
        }
        write(type_flat(type));
    }

    void write_record_type(const ast::RecordType& record) {
        if (record.fields.empty() && record.trailing.comments.empty()) {
            write("{}");
            return;
        }
        const std::string flat = record_type_flat(record);
        if (record.fields.size() <= 1 && !has_comments(record) && fits(flat)) {
            write(flat);
            return;
        }
        write("{");
        newline();
        indent_ += kIndentWidth;
        for (const ast::RecordField& field : record.fields) {
            write_leading(field.leading);
            pad();
            write(field.name.text);
            write(": ");
            if (field.type) {
                write_type(*field.type);
            }
            if (field.refine.has_value()) {
                write(" where ");
                write_expr(**field.refine);
            }
            write(",");
            newline();
        }
        for (const ast::Comment& comment : record.trailing.comments) {
            write_comment(comment);
        }
        indent_ -= kIndentWidth;
        pad();
        write("}");
    }

    // ------------------------------------------------------------- flat render

    [[nodiscard]] std::optional<std::string> flat_simple(const ast::Expr& expr) const {
        if (const auto* lit = std::get_if<ast::Lit>(&expr.kind)) {
            return std::string{lit->text};
        }
        if (const auto* path = std::get_if<ast::Path>(&expr.kind)) {
            return path_text(*path);
        }
        if (const auto* record = std::get_if<ast::RecordLit>(&expr.kind)) {
            return record_flat(*record);
        }
        if (const auto* named = std::get_if<ast::NamedRecord>(&expr.kind)) {
            return path_text(named->type) + " " + record_flat(named->record);
        }
        if (const auto* list = std::get_if<ast::ListLit>(&expr.kind)) {
            return expr_flat_of_list(*list);
        }
        if (const auto* variant = std::get_if<ast::Variant>(&expr.kind)) {
            return "." + std::string{variant->name.text};
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> flat_control(const ast::Expr& expr,
                                                          int parent_precedence) const {
        if (const auto* if_expr = std::get_if<ast::IfExpr>(&expr.kind)) {
            std::string result = "if " + expr_flat(*if_expr->cond) + " ";
            result += block_flat(if_expr->then_block);
            if (if_expr->else_branch.has_value()) {
                result += " else ";
                result += expr_flat(**if_expr->else_branch);
            }
            return result;
        }
        if (const auto* block = std::get_if<ast::Block>(&expr.kind)) {
            return block_flat(*block);
        }
        if (const auto* unary = std::get_if<ast::Unary>(&expr.kind)) {
            const int precedence = unary->op == ast::UnaryOp::Neg ? 7 : 3;
            std::string result = unary->op == ast::UnaryOp::Neg ? "-" : "not ";
            result += expr_flat(*unary->operand, precedence);
            return precedence < parent_precedence ? "(" + result + ")" : result;
        }
        if (const auto* binary = std::get_if<ast::Binary>(&expr.kind)) {
            const int precedence = binary_precedence(binary->op);
            std::string result = expr_flat(*binary->lhs, precedence);
            result += ' ';
            result += binary_spelling(binary->op);
            result += ' ';
            result += expr_flat(*binary->rhs, precedence + 1);
            return precedence < parent_precedence ? "(" + result + ")" : result;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string flat_lambda(const ast::Lambda& lambda) const {
        std::string params;
        if (lambda.params.size() == 1 && !lambda.parenthesised && !lambda.trailing) {
            params = std::string{lambda.params.front().text};
        } else {
            std::vector<std::string> names;
            names.reserve(lambda.params.size());
            for (const ast::Ident& param : lambda.params) {
                names.emplace_back(param.text);
            }
            params = "(" + join(names, ", ") + ")";
        }
        if (lambda.trailing) {
            return "{ " + params + " => " + expr_flat(*lambda.body) + " }";
        }
        return params + " => " + expr_flat(*lambda.body);
    }

    [[nodiscard]] std::string flat_interp(const ast::Interp& interp) const {
        std::string result{interp.open};
        for (const ast::InterpPart& part : interp.parts) {
            if (const auto* text = std::get_if<std::string_view>(&part)) {
                result += *text;
            } else {
                result += '{';
                result += expr_flat(*std::get<ast::ExprPtr>(part));
                result += '}';
            }
        }
        result += interp.close;
        return result;
    }

    [[nodiscard]] std::optional<std::string> flat_postfix(const ast::Expr& expr) const {
        if (const auto* call = std::get_if<ast::Call>(&expr.kind)) {
            std::string result = expr_flat(*call->callee, 9) + "(";
            result += args_flat(call->args);
            result += ')';
            if (call->trailing.has_value()) {
                result += " " + expr_flat(**call->trailing, 9);
            }
            return result;
        }
        if (const auto* method = std::get_if<ast::Method>(&expr.kind)) {
            std::string result = expr_flat(*method->receiver, 9) + "." +
                                 std::string{method->name.text} + "(" + args_flat(method->args) +
                                 ")";
            if (method->trailing.has_value()) {
                result += " " + expr_flat(**method->trailing, 9);
            }
            return result;
        }
        if (const auto* field = std::get_if<ast::Field>(&expr.kind)) {
            return expr_flat(*field->receiver, 9) + "." + std::string{field->name.text};
        }
        if (const auto* lambda = std::get_if<ast::Lambda>(&expr.kind)) {
            return flat_lambda(*lambda);
        }
        if (const auto* ask = std::get_if<ast::Ask>(&expr.kind)) {
            return "ask " + expr_flat(*ask->target, 9) + "(" + args_flat(ask->args) + ")";
        }
        if (const auto* fail = std::get_if<ast::FailExpr>(&expr.kind)) {
            std::string result = "fail " + std::string{fail->kind.text};
            if (!fail->args.empty()) {
                std::vector<std::string> parts;
                parts.reserve(fail->args.size());
                for (const ast::ExprPtr& arg : fail->args) {
                    parts.push_back(expr_flat(*arg));
                }
                result += "(" + join(parts, ", ") + ")";
            }
            return result;
        }
        if (const auto* complete = std::get_if<ast::Complete>(&expr.kind)) {
            return "complete " + expr_flat(*complete->value);
        }
        if (const auto* interp = std::get_if<ast::Interp>(&expr.kind)) {
            return flat_interp(*interp);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string expr_flat(const ast::Expr& expr, int parent_precedence = 0) const {
        if (auto simple = flat_simple(expr)) {
            return *simple;
        }
        if (auto control = flat_control(expr, parent_precedence)) {
            return *control;
        }
        if (auto postfix = flat_postfix(expr)) {
            return *postfix;
        }
        return {};
    }

    [[nodiscard]] std::string args_flat(const std::vector<ast::Arg>& args) const {
        std::vector<std::string> parts;
        parts.reserve(args.size());
        for (const ast::Arg& arg : args) {
            if (const auto* named = std::get_if<ast::NamedArg>(&arg)) {
                parts.push_back(std::string{named->name.text} + ": " + expr_flat(*named->value));
            } else {
                parts.push_back(expr_flat(*std::get<ast::ExprPtr>(arg)));
            }
        }
        return join(parts, ", ");
    }

    [[nodiscard]] std::string block_flat(const ast::Block& block) const {
        std::vector<std::string> parts;
        for (const ast::Stmt& stmt : block.stmts) {
            if (const auto* let = std::get_if<ast::LetStmt>(&stmt)) {
                std::string part = "let " + std::string{let->name.text};
                if (let->type) {
                    part += ": " + type_flat(*let->type);
                }
                part += " = " + expr_flat(*let->value);
                parts.push_back(std::move(part));
            } else if (const auto* expr = std::get_if<ast::ExprStmt>(&stmt)) {
                parts.push_back(expr_flat(*expr->expr));
            }
        }
        if (block.tail.has_value()) {
            parts.push_back(expr_flat(**block.tail));
        }
        if (parts.empty()) {
            return "{}";
        }
        return "{ " + join(parts, "; ") + " }";
    }

    [[nodiscard]] std::string record_flat(const ast::RecordLit& record) const {
        std::vector<std::string> parts;
        parts.reserve(record.fields.size());
        for (const ast::FieldInit& field : record.fields) {
            std::string part{field.name.text};
            if (field.value.has_value()) {
                part += ": " + expr_flat(**field.value);
            }
            parts.push_back(std::move(part));
        }
        if (parts.empty()) {
            return "{}";
        }
        return "{ " + join(parts, ", ") + " }";
    }

    [[nodiscard]] std::string expr_flat_of_list(const ast::ListLit& list) const {
        std::vector<std::string> parts;
        parts.reserve(list.elems.size());
        for (const ast::ExprPtr& element : list.elems) {
            parts.push_back(expr_flat(*element));
        }
        return "[" + join(parts, ", ") + "]";
    }

    [[nodiscard]] std::string type_flat(const ast::TypeExpr& type) const {
        if (const auto* named = std::get_if<ast::NamedType>(&type.kind)) {
            return path_text(named->path);
        }
        if (const auto* list = std::get_if<ast::ListType>(&type.kind)) {
            return list->elem ? "[" + type_flat(*list->elem) + "]" : "[]";
        }
        if (const auto* optional = std::get_if<ast::OptionalType>(&type.kind)) {
            return optional->inner ? type_flat(*optional->inner) + "?" : "?";
        }
        if (const auto* record = std::get_if<ast::RecordType>(&type.kind)) {
            return record_type_flat(*record);
        }
        return {};
    }

    [[nodiscard]] std::string record_type_flat(const ast::RecordType& record) const {
        std::vector<std::string> parts;
        parts.reserve(record.fields.size());
        for (const ast::RecordField& field : record.fields) {
            std::string part{field.name.text};
            part += ": ";
            part += field.type ? type_flat(*field.type) : std::string{"Unknown"};
            if (field.refine.has_value()) {
                part += " where ";
                part += expr_flat(**field.refine);
            }
            parts.push_back(std::move(part));
        }
        if (parts.empty()) {
            return "{}";
        }
        return "{ " + join(parts, ", ") + " }";
    }

    std::string out_;
    int indent_ = 0;
    std::size_t column_ = 0;
};

} // namespace

std::string format_program(const ast::Program& program, std::string_view /*src*/) {
    return Printer{}.run(program);
}

} // namespace leash::syntax
