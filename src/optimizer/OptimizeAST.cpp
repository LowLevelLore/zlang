#include "all.hpp"

namespace zust {
    ASTOptimizer::ASTOptimizer() {}
    ASTOptimizer::~ASTOptimizer() {}

    std::unique_ptr<ASTNode>
    ASTOptimizer::optimizeBinaryOperations(std::unique_ptr<ASTNode> binOp) {
        logMessage("BinOP");
        // 1) First, recursively optimize the two operand subtrees
        binOp->children[0] = optimizeExpression(std::move(binOp->children[0]));
        binOp->children[1] = optimizeExpression(std::move(binOp->children[1]));

        ASTNode* left = binOp->children[0].get();
        ASTNode* right = binOp->children[1].get();

        if (!left->isLiteral() || !right->isLiteral())
            return binOp;

        auto toDouble = [&](ASTNode* n) {
            if (n->type == NodeType::FloatLiteral)
                return stod(n->value);
            if (n->type == NodeType::IntegerLiteral)
                return double(stoi(n->value));
            // promote bool → 0/1
            if (n->type == NodeType::BooleanLiteral)
                return n->value == "true" ? 1.0 : 0.0;
            return 0.0;
        };

        auto toInt = [&](ASTNode* n) {
            if (n->type == NodeType::IntegerLiteral)
                return int64_t(stoi(n->value));
            if (n->type == NodeType::BooleanLiteral)
                return n->value == "true" ? int64_t(1) : int64_t(0);
            // (you could truncate floats here, or disallow)
            return int64_t(stod(n->value));
        };

        const std::string& op = binOp->value;
        bool boolResult = false;
        double dblResult = 0.0;
        int64_t iResult = 0;

        // 5) Arithmetic + comparison in one pass
        if (left->isNumericLiteral() && right->isNumericLiteral()) {
            // mixed-type? promote to float
            bool useFloat = (left->type == NodeType::FloatLiteral || right->type == NodeType::FloatLiteral);

            if (useFloat) {
                double L = toDouble(left);
                double R = toDouble(right);

                if (op == "+")
                    dblResult = L + R;
                else if (op == "-")
                    dblResult = L - R;
                else if (op == "*")
                    dblResult = L * R;
                else if (op == "/") {
                    if (R == 0.0)
                        return binOp;
                    dblResult = L / R;
                } else if (op == "==") {
                    boolResult = (L == R);
                    goto RET_BOOL;
                } else if (op == "!=") {
                    boolResult = (L != R);
                    goto RET_BOOL;
                } else if (op == "<") {
                    boolResult = (L < R);
                    goto RET_BOOL;
                } else if (op == "<=") {
                    boolResult = (L <= R);
                    goto RET_BOOL;
                } else if (op == ">") {
                    boolResult = (L > R);
                    goto RET_BOOL;
                } else if (op == ">=") {
                    boolResult = (L >= R);
                    goto RET_BOOL;
                } else
                    goto NO_FOLD;

                {
                    auto out = std::make_unique<ASTNode>(NodeType::FloatLiteral);
                    out->type = NodeType::FloatLiteral;
                    out->scope = binOp->scope;
                    out->value = std::to_string(dblResult);
                    return out;
                }
            } else {
                // both ints or bools promoted to int
                int64_t L = toInt(left);
                int64_t R = toInt(right);

                if (op == "+")
                    iResult = L + R;
                else if (op == "-")
                    iResult = L - R;
                else if (op == "*")
                    iResult = L * R;
                else if (op == "/") {
                    if (R == 0)
                        return binOp;
                    iResult = L / R;
                } else if (op == "==") {
                    boolResult = (L == R);
                    goto RET_BOOL;
                } else if (op == "!=") {
                    boolResult = (L != R);
                    goto RET_BOOL;
                } else if (op == "<") {
                    boolResult = (L < R);
                    goto RET_BOOL;
                } else if (op == "<=") {
                    boolResult = (L <= R);
                    goto RET_BOOL;
                } else if (op == ">") {
                    boolResult = (L > R);
                    goto RET_BOOL;
                } else if (op == ">=") {
                    boolResult = (L >= R);
                    goto RET_BOOL;
                } else
                    goto NO_FOLD;

                // produce integer literal
                {
                    auto out = std::make_unique<ASTNode>(NodeType::IntegerLiteral);
                    out->type = NodeType::IntegerLiteral;
                    out->scope = binOp->scope;
                    out->value = std::to_string(iResult);
                    return out;
                }
            }
        }

        // 6) Pure boolean logic: &&, ||, ==, !=
        if (left->type == NodeType::BooleanLiteral && right->type == NodeType::BooleanLiteral) {
            bool L = left->value == "true";
            bool R = right->value == "true";

            if (op == "&&")
                boolResult = (L && R);
            else if (op == "||")
                boolResult = (L || R);
            else if (op == "==")
                boolResult = (L == R);
            else if (op == "!=")
                boolResult = (L != R);
            else
                goto NO_FOLD;
            {
                auto out = std::make_unique<ASTNode>(NodeType::BooleanLiteral);
                out->type = NodeType::BooleanLiteral;
                out->scope = binOp->scope;
                out->value = boolResult ? "true" : "false";
                return out;
            }
        }

    NO_FOLD:
        return binOp;

    RET_BOOL: {
        auto out = std::make_unique<ASTNode>(NodeType::BooleanLiteral);
        out->scope = binOp->scope;
        out->value = boolResult ? "true" : "false";
        return out;
    }
    }

    std::unique_ptr<ASTNode> ASTOptimizer::optimizeExpression(std::unique_ptr<ASTNode> node) {
        VariableInfo varInfo;
        switch (node->type) {
        case NodeType::BinaryOp:
            return optimizeBinaryOperations(std::move(node));
            break;
        case NodeType::VariableAccess: {
            logMessage("Expression");
            varInfo = node->scope->lookupVariable(node->value);
            if (varInfo.isValueKnown) {
                std::unique_ptr<ASTNode> literalNode = std::make_unique<ASTNode>();
                literalNode->scope = node->scope;
                literalNode->value = varInfo.value;
                if (varInfo.type == "uint8_t" || varInfo.type == "uint16_t" || varInfo.type == "uint32_t" || varInfo.type == "uint64_t" || varInfo.type == "int8_t" || varInfo.type == "int16_t" || varInfo.type == "int32_t" || varInfo.type == "int64_t" || varInfo.type == "size_t" || varInfo.type == "integer") {
                    literalNode->type = NodeType::IntegerLiteral;
                } else if (varInfo.type == "float" || varInfo.type == "double") {
                    literalNode->type = NodeType::FloatLiteral;
                } else if (varInfo.type == "boolean") {
                    literalNode->type = NodeType::BooleanLiteral;
                } else if (varInfo.type == "string") {
                    literalNode->type = NodeType::StringLiteral;
                } else {
                    return node;
                }
                return literalNode;
            } else {
                return node;
            }
        }
        default:
            break;
        }
        return node;
    }

    std::unique_ptr<ASTNode> ASTOptimizer::optimizeStatement(std::unique_ptr<ASTNode> statement) {
        VariableInfo varInfo;
        switch (statement->type) {
        case NodeType::VariableDeclaration: {
            if (statement->children.size() > 1) {
                statement->children[1] = optimizeExpression(std::move(statement->children[1]));
                varInfo = statement->scope->lookupVariable(statement->value);
                if (statement->children[1]->isLiteral()) {
                    varInfo.isValueKnown = true;
                    varInfo.value = statement->children[1]->value;
                } else {
                    varInfo.isValueKnown = false;
                    varInfo.value = "";
                }
                statement->scope->editVariable(statement->value, varInfo);
            }
            break;
        }
        case NodeType::VariableReassignment:
            statement->children[0] = optimizeExpression(std::move(statement->children[0]));
            varInfo = statement->scope->lookupVariable(statement->value);
            if (statement->children[1]->isLiteral()) {
                varInfo.isValueKnown = true;
                varInfo.value = statement->children[0]->value;
            } else {
                varInfo.isValueKnown = false;
                varInfo.value = "";
            }
            statement->scope->editVariable(statement->value, varInfo);
            break;
        case NodeType::VariableAccess:
        case NodeType::BinaryOp:
        case NodeType::UnaryOp:
            return optimizeExpression(std::move(statement));
            break;
        case NodeType::Function: {
            statement->children[2] =
                optimize(std::move(statement->children[2]));
            break;
        }
        default:
            break;
        }
        return statement;
    }

    std::unique_ptr<ASTNode> ASTOptimizer::optimize(std::unique_ptr<ASTNode> program) {
        std::unique_ptr<ASTNode> optimized = std::make_unique<ASTNode>();
        optimized->scope = program->scope;
        std::vector<std::unique_ptr<ASTNode>>& statements = program->children;
        for (std::unique_ptr<ASTNode>& statement : statements) {
            optimized->addChild(optimizeStatement(std::move(statement)));
        }
        return optimized;
    }
}  // namespace zust
