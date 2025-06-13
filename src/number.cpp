#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

// Number implementation
Number::Number(double value) : Expression(SourceLocation()), value(value) {}

Number::Number(const SourceLocation& loc, double value) : Expression(loc), value(value) {}

std::string Number::toString() {
    return std::to_string(value);
}

void Number::printNode(int depth) {
    printIndent(depth);
    std::cout << "└── Number: " << value << "\n";
}

bool Number::Validate(IContext* /*context*/) {
    return true;
}

Number::~Number() {}

llvm::Value* Number::codegen(CodeGenerator& generator) {
    // Use floating point for Number type
    return llvm::ConstantFP::get(llvm::Type::getFloatTy(generator.getContext()), 
                                static_cast<float>(value));
}