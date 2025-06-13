#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>


ExpressionStatement::ExpressionStatement(Expression* expr) 
    : Statement(SourceLocation()), expr(expr) {}

ExpressionStatement::ExpressionStatement(const SourceLocation& loc, Expression* expr)
    : Statement(loc), expr(expr) {}

void ExpressionStatement::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── Expression Statement\n";
    if (expr) {
        expr->printNode(depth + 1);
    }
}

bool ExpressionStatement::Validate(IContext* context) {
    if (!expr->Validate(context)) {
        SEMANTIC_ERROR("Error in expression statement", location);
        return false;
    }
    return true;
}

ExpressionStatement::~ExpressionStatement() {
    delete expr;
}

llvm::Value* ExpressionStatement::codegen(CodeGenerator& generator) {
    if (!expr) {
        // If there's no expression, return a default value (0 for int type)
        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0);
    }
    
    // The value of an ExpressionStatement is the value of the expression it wraps
    llvm::Value* value = expr->codegen(generator);
    
    // If the expression failed to generate code, propagate nullptr
    if (!value) {
        return nullptr;
    }
    
    // Return the actual value of the expression, regardless of its type
    return value;
}
