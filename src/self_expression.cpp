#include "tree.hpp"
#include <iostream>

SelfExpression::SelfExpression(const SourceLocation& loc)
    : Expression(loc) {}

SelfExpression::~SelfExpression() {}

std::string SelfExpression::toString() {
    return "self";
}

void SelfExpression::printNode(int depth) {
    printIndent(depth);
    std::cout << "SelfExpression" << std::endl;
}

bool SelfExpression::Validate(IContext* context) {
    // Check if we're inside a method (self should be defined)
    if (!context->isDefined("self")) {
        SEMANTIC_ERROR("'self' can only be used inside methods", location);
        return false;
    }
    return true;
}

llvm::Value* SelfExpression::codegen(CodeGenerator& generator) {
    std::cout << "Generating code for self expression" << std::endl;
    
    // Get the 'self' value from the current scope
    llvm::Value* selfValue = generator.getNamedValue(std::string("self"));
    if (!selfValue) {
        SEMANTIC_ERROR("'self' is not defined in this context", location);
        return nullptr;
    }
    
    return selfValue;
}