#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

ForStatement::ForStatement(Statement* init_stmt, Expression* cond, 
                         Statement* inc, BlockStatement* body_block)
    : Statement(SourceLocation()), init(init_stmt), condition(cond), 
      increment(inc), body(body_block) {}

ForStatement::ForStatement(const SourceLocation& loc, Statement* init_stmt, 
                         Expression* cond, Statement* inc, BlockStatement* body_block)
    : Statement(loc), init(init_stmt), condition(cond), 
      increment(inc), body(body_block) {}

void ForStatement::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── For Statement\n";
    if (init) {
        printIndent(depth);
        std::cout << "│   ├── Initialization:\n";
        init->printNode(depth + 2);
    }
    if (condition) {
        printIndent(depth);
        std::cout << "│   ├── Condition:\n";
        condition->printNode(depth + 2);
    }
    if (increment) {
        printIndent(depth);
        std::cout << "│   ├── Increment:\n";
        increment->printNode(depth + 2);
    }
    printIndent(depth);
    std::cout << "│   └── Body:\n";
    body->printNode(depth + 2);
}

bool ForStatement::Validate(IContext* context) {
    if (init && !init->Validate(context)) {
        SEMANTIC_ERROR("Error in for loop initialization", location);
        return false;
    }
    if (condition && !condition->Validate(context)) {
        SEMANTIC_ERROR("Error in for loop condition", location);
        return false;
    }
    if (increment && !increment->Validate(context)) {
        SEMANTIC_ERROR("Error in for loop increment", location);
        return false;
    }
    if (!body->Validate(context)) {
        SEMANTIC_ERROR("Error in for loop body", location);
        return false;
    }
    return true;
}

ForStatement::~ForStatement() {
    if (init) delete init;
    if (condition) delete condition;
    if (increment) delete increment;
    delete body;
}

llvm::Value* ForStatement::codegen(CodeGenerator& generator) {
    // 1. Generar inicialización
    if (init && !init->codegen(generator)) {
        return nullptr;
    }

    // 2. Obtener función actual
    llvm::Function* func = generator.getBuilder()->GetInsertBlock()->getParent();
    
    // 3. Crear bloques básicos
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(generator.getContext(), "for.cond", func);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(generator.getContext(), "for.body", func);
    llvm::BasicBlock* incBB = llvm::BasicBlock::Create(generator.getContext(), "for.inc", func);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(generator.getContext(), "for.end", func);
    
    // Create an alloca to store the result of each iteration
    llvm::AllocaInst* resultAlloca = generator.createEntryBlockAlloca(
        func, "for.result", llvm::Type::getFloatTy(generator.getContext()));
    
    // Initialize with default value (0.0)
    generator.getBuilder()->CreateStore(
        llvm::ConstantFP::get(llvm::Type::getFloatTy(generator.getContext()), 0.0),
        resultAlloca);
    
    // 4. Saltar a condición
    generator.getBuilder()->CreateBr(condBB);
    
    // 5. Generar condición
    generator.getBuilder()->SetInsertPoint(condBB);
    llvm::Value* condVal = condition ? condition->codegen(generator) : 
                                     llvm::ConstantInt::getTrue(generator.getContext());
    generator.getBuilder()->CreateCondBr(condVal, bodyBB, endBB);
    
    // 6. Generar cuerpo
    generator.getBuilder()->SetInsertPoint(bodyBB);
    llvm::Value* bodyResult = body->codegen(generator);
    if (!bodyResult) {
        return nullptr;
    }
    
    // Store the result of the body execution (this will be the last expression's value)
    // Convert to float if necessary
    if (bodyResult->getType()->isIntegerTy()) {
        bodyResult = generator.getBuilder()->CreateSIToFP(
            bodyResult, llvm::Type::getFloatTy(generator.getContext()));
    }
    generator.getBuilder()->CreateStore(bodyResult, resultAlloca);
    
    generator.getBuilder()->CreateBr(incBB);
    
    // 7. Generar incremento
    generator.getBuilder()->SetInsertPoint(incBB);
    if (increment && !increment->codegen(generator)) {
        return nullptr;
    }
    generator.getBuilder()->CreateBr(condBB);
    
    // 8. Establecer punto de inserción al final
    generator.getBuilder()->SetInsertPoint(endBB);
    
    // Load and return the final result
    return generator.getBuilder()->CreateLoad(
        llvm::Type::getFloatTy(generator.getContext()), resultAlloca, "for.final.result");
}
