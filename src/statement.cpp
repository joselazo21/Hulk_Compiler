#include "tree.hpp"
#include <error_handler.hpp>

Statement::Statement(const SourceLocation& loc) : Node(loc) {}

void Statement::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── Statement\n";
}

bool Statement::Validate(IContext* context) {
    // Base implementation
    return true;
}

llvm::Value* Statement::codegen(CodeGenerator& generator) {
    // Base implementation returns nullptr
    return nullptr;
}
