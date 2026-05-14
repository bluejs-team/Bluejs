#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// ASTNode - carries the esprima node type, the original JSON (so the emitter
// can read any field it needs), and the recursively-built child list.
struct ASTNode {
    std::string          type;     // esprima type string, e.g. "Program"
    nlohmann::json       data;     // full esprima JSON object for this node
    std::vector<ASTNode> children; // ordered child nodes
};
