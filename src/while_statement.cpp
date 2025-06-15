#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

// WhileStatement implementation
WhileStatement::WhileStatement(Expression* cond, BlockStatement* body_block)
    : Statement(SourceLocation()), condition(cond), body(body_block) {}

WhileStatement::WhileStatement(const SourceLocation& loc, Expression* cond, BlockStatement* body_block)
    : Statement(loc), condition(cond), body(body_block) {}

void WhileStatement::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── While Statement\n";
    printIndent(depth);
    std::cout << "│   ├── Condition:\n";
    condition->printNode(depth + 2);
    printIndent(depth);
    std::cout << "│   └── Body:\n";
    body->printNode(depth + 2);
}

bool WhileStatement::Validate(IContext* context) {
    if (!condition->Validate(context)) {
        SEMANTIC_ERROR("Error in while condition", location);
        return false;
    }
    if (!body->Validate(context)) {
        SEMANTIC_ERROR("Error in while loop body", location);
        return false;
    }
    return true;
}

WhileStatement::~WhileStatement() {
    delete condition;
    delete body;
}

llvm::Value* WhileStatement::codegen(CodeGenerator& generator) {
    llvm::Function* func = generator.getBuilder()->GetInsertBlock()->getParent();

    std::cout << "ENTRO ACA";

    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(generator.getContext(), "while.cond", func);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(generator.getContext(), "while.body", func);
    llvm::BasicBlock* endBB = llvm::BasicBlock::Create(generator.getContext(), "while.end", func);

    llvm::Value* resultPtr = generator.getBuilder()->CreateAlloca(
        llvm::Type::getFloatTy(generator.getContext()), nullptr, "while.result");

    generator.getBuilder()->CreateBr(condBB);

    generator.getBuilder()->SetInsertPoint(condBB);
    llvm::Value* condVal = condition->codegen(generator);
    
    // Convert condition to boolean (i1)
    if (condVal->getType()->isIntegerTy(1)) {
        // Already a boolean, use as is
    } else if (condVal->getType()->isIntegerTy()) {
        // Integer type - compare with zero of the same type
        llvm::Type* condType = condVal->getType();
        condVal = generator.getBuilder()->CreateICmpNE(
            condVal, llvm::ConstantInt::get(condType, 0), "whilecond");
    } else {
        // For other types, convert to i32 first, then compare
        if (condVal->getType()->isDoubleTy() || condVal->getType()->isFloatTy()) {
            condVal = generator.getBuilder()->CreateFCmpONE(condVal, 
                llvm::ConstantFP::get(condVal->getType(), 0.0), "whilecond");
        } else {
            condVal = generator.getBuilder()->CreateICmpNE(
                condVal, llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0), "whilecond");
        }
    }
    
    generator.getBuilder()->CreateCondBr(condVal, bodyBB, endBB);

    generator.getBuilder()->SetInsertPoint(bodyBB);
    llvm::BasicBlock* currentBodyBB = generator.getBuilder()->GetInsertBlock();
    llvm::Value* lastValue = body->codegen(generator);

    if (lastValue && !lastValue->getType()->isVoidTy()) {
        if (!currentBodyBB->getTerminator()) {
            generator.getBuilder()->CreateStore(lastValue, resultPtr);
        }
    }

    // Solo añade br condBB si el bloque no fue terminado por el body
    if (!currentBodyBB->getTerminator()) {
        std::cout << "Inserto terminator";
        generator.getBuilder()->CreateBr(condBB);
    }

    generator.getBuilder()->SetInsertPoint(endBB);
    // Eliminado: return automático en while/endBB (sea o no main)
    return nullptr;
}