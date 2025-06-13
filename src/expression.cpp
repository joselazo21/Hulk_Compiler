
#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>


Expression::Expression(const SourceLocation& loc) : Node(loc) {}

llvm::Value* Expression::codegen(CodeGenerator& generator) {
    // Base implementation returns nullptr
    return nullptr;
}