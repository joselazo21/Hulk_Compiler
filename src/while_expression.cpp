#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>


WhileExpression::WhileExpression(Expression* cond, BlockExpression* body)
    : Expression(SourceLocation()), condition(cond), body(body) {}

WhileExpression::WhileExpression(const SourceLocation& loc, Expression* cond, BlockExpression* body)
    : Expression(loc), condition(cond), body(body) {}

WhileExpression::~WhileExpression() {
    delete condition;
    delete body;
}

std::string WhileExpression::toString() {
    return "while (" + condition->toString() + ") " + body->toString();
}

void WhileExpression::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── While Expression\n";
    printIndent(depth);
    std::cout << "│   ├── Condition:\n";
    condition->printNode(depth + 2);
    printIndent(depth);
    std::cout << "│   └── Body:\n";
    body->printNode(depth + 2);
}

bool WhileExpression::Validate(IContext* context) {
    if (!condition->Validate(context)) {
        SEMANTIC_ERROR("Error in while-expression condition", location);
        return false;
    }
    if (!body->Validate(context)) {
        SEMANTIC_ERROR("Error in while-expression body", location);
        return false;
    }
    return true;
}

llvm::Value* WhileExpression::codegen(CodeGenerator& generator) {
    std::cout << "[LOG] WhileExpression::codegen START\n";
    llvm::IRBuilder<>* builder = generator.getBuilder();
    llvm::Function* parentFunction = builder->GetInsertBlock()->getParent();
    llvm::LLVMContext& context = generator.getContext();

    // Create a PHI node to track the last value from the loop
    llvm::AllocaInst* lastValuePtr = builder->CreateAlloca(
        llvm::Type::getInt32Ty(context), nullptr, "while.lastvalue");
    builder->CreateStore(
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0), 
        lastValuePtr);

    // Create basic blocks
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(context, "while.cond", parentFunction);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "while.body", parentFunction);
    llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(context, "while.exit", parentFunction);

    // Jump to condition
    builder->CreateBr(condBB);

    // Condition block
    builder->SetInsertPoint(condBB);
    llvm::Value* condValue = condition->codegen(generator);
    if (!condValue) {
        SEMANTIC_ERROR("Error generating while condition", location);
        return nullptr;
    }
    
    // Ensure condition is boolean
    llvm::Value* condBool = condValue;
    if (condValue->getType()->isIntegerTy(1)) {
        // Already a boolean, use as is
        condBool = condValue;
    } else if (condValue->getType()->isIntegerTy()) {
        // Integer type - compare with zero of the same type
        llvm::Type* condType = condValue->getType();
        condBool = builder->CreateICmpNE(condValue, llvm::ConstantInt::get(condType, 0), "whilecond");
    } else {
        // For other types, convert to i32 first, then compare
        if (condValue->getType()->isDoubleTy() || condValue->getType()->isFloatTy()) {
            condValue = builder->CreateFPToSI(condValue, llvm::Type::getInt32Ty(context), "fp_to_int");
        }
        condBool = builder->CreateICmpNE(condValue, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0), "whilecond");
    }
    
    // Branch to body or exit
    builder->CreateCondBr(condBool, bodyBB, exitBB);

    // Body block
    builder->SetInsertPoint(bodyBB);
    llvm::Value* bodyValue = body->codegen(generator);
    if (!bodyValue) {
        SEMANTIC_ERROR("Error generating while body", location);
        return nullptr;
    }
    
    // Store the body value as the last value
    if (bodyValue->getType()->isIntegerTy(32)) {
        builder->CreateStore(bodyValue, lastValuePtr);
    }
    
    // Get current block after body generation
    llvm::BasicBlock* currentBodyBB = builder->GetInsertBlock();
    
    // If body doesn't have terminator, branch back to condition
    if (!currentBodyBB->getTerminator()) {
        builder->CreateBr(condBB);
        std::cout << "[LOG] WhileExpression::codegen, adding branch back to condition\n";
    }

    // Exit block
    builder->SetInsertPoint(exitBB);
    
    // Load and return the last value
    llvm::Value* result = builder->CreateLoad(
        llvm::Type::getInt32Ty(context), 
        lastValuePtr, 
        "while.result");
    
    std::cout << "[LOG] WhileExpression::codegen END, returning last value\n";
    
    return result;
}
