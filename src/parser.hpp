#pragma once
#include "ast_node.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

// Parser - converts an esprima JSON AST into an ASTNode tree.
//
// Supported esprima node types (basic JS subset):
//   Program, VariableDeclaration, VariableDeclarator,
//   ExpressionStatement, CallExpression, MemberExpression,
//   BinaryExpression, UnaryExpression, AssignmentExpression,
//   LogicalExpression, UpdateExpression,
//   FunctionDeclaration, FunctionExpression, BlockStatement, ReturnStatement,
//   IfStatement, WhileStatement, DoWhileStatement, ForStatement,
//   TryStatement, CatchClause, ThrowStatement, SwitchStatement, SwitchCase,
//   BreakStatement, ContinueStatement, EmptyStatement,
//   ConditionalExpression, SequenceExpression, ThisExpression,
//   Identifier, Literal
class Parser {
public:
    static ASTNode parseNode(const nlohmann::json& j) {
        if (!j.is_object()) {
            throw std::runtime_error(
                "Parser::parseNode: expected JSON object, got " +
                std::string(j.type_name()));
        }

        ASTNode node;
        node.data = j;
        node.type = j.contains("type") && j["type"].is_string()
                    ? j["type"].get<std::string>()
                    : "<unknown>";

        // ── Dispatch on node type ─────────────────────────────────────────────
        if (node.type == "Program") {
            appendArray(j, "body", node);

        } else if (node.type == "VariableDeclaration") {
            appendArray(j, "declarations", node);

        } else if (node.type == "VariableDeclarator") {
            // id  → Identifier (the variable name)
            // init → initializer expression (may be null)
            appendObject(j, "id",   node);
            appendObject(j, "init", node);

        } else if (node.type == "ExpressionStatement") {
            appendObject(j, "expression", node);

        } else if (node.type == "CallExpression") {
            // callee, then each argument
            appendObject(j, "callee", node);
            appendArray(j, "arguments", node);

        } else if (node.type == "MemberExpression") {
            appendObject(j, "object",   node);
            appendObject(j, "property", node);

        } else if (node.type == "BinaryExpression" ||
                   node.type == "LogicalExpression" ||
                   node.type == "AssignmentExpression") {
            appendObject(j, "left",  node);
            appendObject(j, "right", node);

        } else if (node.type == "UnaryExpression") {
            appendObject(j, "argument", node);

        } else if (node.type == "UpdateExpression") {
            appendObject(j, "argument", node);

        } else if (node.type == "FunctionDeclaration" ||
                   node.type == "FunctionExpression") {
            appendObject(j, "id",   node);
            appendArray(j,  "params", node);
            appendObject(j, "body", node);

        } else if (node.type == "ConditionalExpression") {
            appendObject(j, "test",       node);
            appendObject(j, "consequent", node);
            appendObject(j, "alternate",  node);

        } else if (node.type == "SequenceExpression") {
            appendArray(j, "expressions", node);

        } else if (node.type == "ThisExpression") {
            /* leaf */

        } else if (node.type == "TryStatement") {
            appendObject(j, "block", node);
            if (j.contains("handler") && !j["handler"].is_null())
                appendObject(j, "handler", node);
            if (j.contains("finalizer") && !j["finalizer"].is_null())
                appendObject(j, "finalizer", node);

        } else if (node.type == "CatchClause") {
            if (j.contains("param") && !j["param"].is_null())
                appendObject(j, "param", node);
            appendObject(j, "body", node);

        } else if (node.type == "ThrowStatement") {
            if (j.contains("argument") && !j["argument"].is_null())
                appendObject(j, "argument", node);

        } else if (node.type == "SwitchStatement") {
            appendObject(j, "discriminant", node);
            appendArray(j, "cases", node);

        } else if (node.type == "SwitchCase") {
            if (j.contains("test") && !j["test"].is_null())
                appendObject(j, "test", node);
            appendArray(j, "consequent", node);

        } else if (node.type == "DoWhileStatement") {
            appendObject(j, "body", node);
            appendObject(j, "test", node);

        } else if (node.type == "BreakStatement") {
            if (j.contains("label") && !j["label"].is_null())
                appendObject(j, "label", node);

        } else if (node.type == "ContinueStatement") {
            if (j.contains("label") && !j["label"].is_null())
                appendObject(j, "label", node);

        } else if (node.type == "EmptyStatement") {
            /* leaf */

        } else if (node.type == "BlockStatement") {
            appendArray(j, "body", node);

        } else if (node.type == "ReturnStatement") {
            if (j.contains("argument") && !j["argument"].is_null()) {
                appendObject(j, "argument", node);
            }

        } else if (node.type == "IfStatement") {
            appendObject(j, "test",       node);
            appendObject(j, "consequent", node);
            if (j.contains("alternate") && !j["alternate"].is_null()) {
                appendObject(j, "alternate", node);
            }

        } else if (node.type == "WhileStatement") {
            appendObject(j, "test", node);
            appendObject(j, "body", node);

        } else if (node.type == "ForStatement") {
            for (const char* k : {"init", "test", "update", "body"}) {
                if (j.contains(k) && !j[k].is_null()) {
                    appendObject(j, k, node);
                }
            }

        } else if (node.type == "ObjectExpression") {
            appendArray(j, "properties", node);

        } else if (node.type == "ObjectPattern") {
            appendArray(j, "properties", node);

        } else if (node.type == "Property") {
            // key, then value (shorthand: both are the same Identifier)
            appendObject(j, "key",   node);
            appendObject(j, "value", node);

        } else if (node.type == "ArrayExpression") {
            appendArray(j, "elements", node);

        } else if (node.type == "SpreadElement") {
            appendObject(j, "argument", node);

        } else if (node.type == "ArrayPattern") {
            appendArray(j, "elements", node);

        } else if (node.type == "RestElement") {
            appendObject(j, "argument", node);

        } else if (node.type == "AssignmentPattern") {
            appendObject(j, "left",  node);
            appendObject(j, "right", node);

        } else if (node.type == "ForOfStatement" ||
                   node.type == "ForInStatement") {
            appendObject(j, "left",  node);
            appendObject(j, "right", node);
            appendObject(j, "body",  node);

        } else if (node.type == "ArrowFunctionExpression") {
            appendArray(j,  "params", node);
            appendObject(j, "body",   node);

        } else if (node.type == "TemplateLiteral") {
            // Interleave quasis and expressions so the emitter can walk in order.
            const auto& qs = j.contains("quasis") && j["quasis"].is_array()
                             ? j["quasis"] : nlohmann::json::array();
            const auto& es = j.contains("expressions") && j["expressions"].is_array()
                             ? j["expressions"] : nlohmann::json::array();
            size_t qn = qs.size(), en = es.size();
            for (size_t qi = 0; qi < qn; ++qi) {
                if (qs[qi].is_object()) node.children.push_back(parseNode(qs[qi]));
                if (qi < en && es[qi].is_object()) node.children.push_back(parseNode(es[qi]));
            }

        } else if (node.type == "TemplateElement") {
            // leaf - cooked/raw values are in node.data["value"]

        } else if (node.type == "ClassDeclaration" ||
                   node.type == "ClassExpression") {
            if (j.contains("id") && j["id"].is_object())
                appendObject(j, "id", node);
            if (j.contains("superClass") && j["superClass"].is_object())
                appendObject(j, "superClass", node);
            if (j.contains("body") && j["body"].is_object())
                appendObject(j, "body", node);

        } else if (node.type == "ClassBody") {
            appendArray(j, "body", node);

        } else if (node.type == "MethodDefinition") {
            appendObject(j, "key",   node);
            appendObject(j, "value", node);

        } else if (node.type == "ImportExpression") {
            if (j.contains("source") && !j["source"].is_null())
                appendObject(j, "source", node);

        } else if (node.type == "Identifier" || node.type == "Literal") {
            // leaf nodes - no children, data already stored above

        } else {
            // Unknown node: still recurse into common child-bearing fields so
            // we don't silently drop subtrees.
            for (const char* k : {"body", "declarations", "expression",
                                   "argument", "consequent", "alternate",
                                   "callee", "arguments", "left", "right",
                                   "init", "id", "params"}) {
                if (!j.contains(k) || j[k].is_null()) continue;
                if (j[k].is_array())       appendArray(j, k, node);
                else if (j[k].is_object()) appendObject(j, k, node);
            }
        }

        return node;
    }

private:
    static void appendArray(const nlohmann::json& j,
                            const char* key,
                            ASTNode& parent) {
        if (!j.contains(key) || !j[key].is_array()) return;
        for (const auto& item : j[key]) {
            if (item.is_object()) parent.children.push_back(parseNode(item));
        }
    }

    static void appendObject(const nlohmann::json& j,
                             const char* key,
                             ASTNode& parent) {
        if (!j.contains(key) || !j[key].is_object()) return;
        parent.children.push_back(parseNode(j[key]));
    }
};
