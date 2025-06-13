#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

// Variable implementation
Variable::Variable(const std::string& name) 
    : Expression(SourceLocation()), name(name) {}

Variable::Variable(const SourceLocation& loc, const std::string& name)
    : Expression(loc), name(name) {}

std::string Variable::toString() {
    return name;
}

void Variable::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── Variable: " << name << "\n";
}

bool Variable::Validate(IContext* context) {
    if (!context->isDefined(name)) {
        SEMANTIC_ERROR("Variable '" + name + "' is not defined", location);
        return false;
    }
    return true;
}

Variable::~Variable() {}

llvm::Value* Variable::codegen(CodeGenerator& generator) {
    // Look up the name in the symbol table
    llvm::Value* val = generator.getNamedValue(name);
    llvm::AllocaInst* alloca = llvm::dyn_cast<llvm::AllocaInst>(val);
    if (!alloca) {
        SEMANTIC_ERROR("Unknown variable name: " + name, location);
        return nullptr;
    }
    
    // Load the value from the alloca
    return generator.getBuilder()->CreateLoad(alloca->getAllocatedType(), alloca, name.c_str());
}