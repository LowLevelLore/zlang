#pragma once

#include <memory>
#include <set>
#include <string>

#include "ast/ASTNode.hpp"

namespace zust {
    class ASTOptimizer {
        std::unique_ptr<ASTNode> optimizeStatement(std::unique_ptr<ASTNode> statement);
        std::unique_ptr<ASTNode> optimizeExpression(std::unique_ptr<ASTNode> node);
        std::unique_ptr<ASTNode> optimizeBinaryOperations(std::unique_ptr<ASTNode> binOp);

    public:
        ASTOptimizer();
        ~ASTOptimizer();

        std::unique_ptr<ASTNode> optimize(std::unique_ptr<ASTNode> program);
    };
}  // namespace zust
