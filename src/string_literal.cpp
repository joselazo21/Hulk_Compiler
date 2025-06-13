#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>


StringLiteral::StringLiteral(const std::string& value) 
    : Expression(SourceLocation()), value(value) {}

StringLiteral::StringLiteral(const SourceLocation& loc, const std::string& value)
    : Expression(loc), value(value) {}

std::string StringLiteral::toString() {
    return "\"" + value + "\"";
}

void StringLiteral::printNode(int depth) {
    printIndent(depth);
    std::cout << "└── String: \"" << value << "\"\n";
}

bool StringLiteral::Validate(IContext* /*context*/) {
    return true;
}

StringLiteral::~StringLiteral() {}

llvm::Value* StringLiteral::codegen(CodeGenerator& generator) {
    return generator.getBuilder()->CreateGlobalStringPtr(value);
}