
#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

BlockExpression::BlockExpression(const SourceLocation& loc, const std::vector<Statement*>& stmts)
    : Expression(loc), statements(stmts) {}

BlockExpression::~BlockExpression() {
    for (auto* stmt : statements) delete stmt;
}

std::string BlockExpression::toString() {
    return "{ ... }";
}

void BlockExpression::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── Block Expression\n";
    for (auto* stmt : statements) {
        stmt->printNode(depth + 1);
    }
}

bool BlockExpression::Validate(IContext* context) {
    IContext* blockContext = context->createChildContext();
    for (auto* stmt : statements) {
        if (!stmt->Validate(blockContext)) {
            delete blockContext;
            return false;
        }
    }
    delete blockContext;
    return true;
}

// WhileExpression implementation

llvm::Value* BlockExpression::codegen(CodeGenerator& generator) {
    std::cout << "[LOG] BlockExpression::codegen START, statements.size()=" << statements.size() << "\n";
    
    llvm::IRBuilder<>* builder = generator.getBuilder();
    llvm::BasicBlock* currentBlock = builder->GetInsertBlock();
    
    llvm::Value* lastValue = nullptr;
    generator.pushScope();
    
    for (auto* stmt : statements) {
        if (!stmt) continue;
        
        std::cout << "[LOG] BlockExpression::codegen, stmt=" << stmt << "\n";
        llvm::Value* stmtValue = stmt->codegen(generator);
        
        if (!stmtValue) {
            std::cerr << "Error: se intentó generar código para una expresión inválida en BlockExpression" << std::endl;
            // Don't return nullptr immediately, try to continue with a default value
            stmtValue = llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0);
        }
        
        // Keep track of the last non-void value
        if (stmtValue->getType() != llvm::Type::getVoidTy(generator.getContext())) {
            lastValue = stmtValue;
        }
        
        std::cout << "[LOG] BlockExpression::codegen, stmt->codegen returned " << stmtValue << "\n";
        
        // Check for terminator after generating statement
        currentBlock = builder->GetInsertBlock();
        if (currentBlock && currentBlock->getTerminator()) {
            std::cout << "[LOG] BlockExpression::codegen, block has terminator after stmt, breaking\n";
            break;
        }
        
        // Special handling for assignments - get the actual value
        if (auto* assignment = dynamic_cast<Assignment*>(stmt)) {
            if (currentBlock && !currentBlock->getTerminator()) {
                const std::string* assignedVar = assignment->getLastAssignedVariable();
                if (assignedVar) {
                    llvm::Value* varPtr = generator.getNamedValue(*assignedVar);
                    if (varPtr && llvm::isa<llvm::AllocaInst>(varPtr)) {
                        llvm::AllocaInst* alloca = llvm::cast<llvm::AllocaInst>(varPtr);
                        llvm::Type* allocatedType = alloca->getAllocatedType();
                        llvm::Value* loadedValue = generator.getBuilder()->CreateLoad(
                            allocatedType,  // Use the actual allocated type instead of hardcoded i32
                            varPtr,
                            "load_assigned"
                        );
                        lastValue = loadedValue;
                        std::cout << "[LOG] BlockExpression::codegen, assignment load value " << loadedValue << "\n";
                    }
                }
            }
        }
    }
    
    if (!lastValue) {
        lastValue = llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0);
    }
    
    std::cout << "[LOG] BlockExpression::codegen END, returning " << lastValue << "\n";
    generator.popScope();
    return lastValue;
}