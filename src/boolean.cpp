#include "tree.hpp"
#include "error_handler.hpp"
#include "type_system.hpp"
#include <iostream>

// Boolean constructors
Boolean::Boolean(bool value) : Expression(), value(value) {}

Boolean::Boolean(const SourceLocation& loc, bool value) : Expression(loc), value(value) {}

// Boolean destructor
Boolean::~Boolean() {}

// toString method
std::string Boolean::toString() {
    return value ? "true" : "false";
}

// printNode method
void Boolean::printNode(int depth) {
    printIndent(depth);
    std::cout << "Boolean: " << (value ? "true" : "false") << std::endl;
}

// Validate method
bool Boolean::Validate(IContext* /*context*/) {
    // Boolean literals are always valid
    return true;
}

// Code generation method
llvm::Value* Boolean::codegen(CodeGenerator& generator) {
    llvm::LLVMContext& context = generator.getContext();
    
    // Create a boolean constant (i1 type in LLVM)
    return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), value ? 1 : 0);
}