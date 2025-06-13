#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

Print::Print(Expression* expr) 
    : Expression(SourceLocation()), expr(expr) {}

Print::Print(const SourceLocation& loc, Expression* expr) 
    : Expression(loc), expr(expr) {}

std::string Print::toString() {
    return "print(" + (expr ? expr->toString() : "") + ")";
}

void Print::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── Print Expression:\n";
    if (expr) expr->printNode(depth + 1);
}

bool Print::Validate(IContext* context) {
    if (!expr->Validate(context)) {
        SEMANTIC_ERROR("Error in print expression", location);
        return false;
    }
    return true;
}

Print::~Print() {
    delete expr;
}

llvm::Value* Print::codegen(CodeGenerator& generator) {
    if (!expr) return nullptr;
    llvm::Value* val = expr->codegen(generator);
    if (!val) return nullptr;
    return generator.generatePrintCall(val);
}