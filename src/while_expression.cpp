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

    // Determine the appropriate type for the last value based on function return type
    llvm::Type* valueType = llvm::Type::getFloatTy(context); // Default to float for Number type
    llvm::Value* defaultValue = llvm::ConstantFP::get(valueType, 0.0);
    
    // Check if we're in a function and get its return type
    if (parentFunction && parentFunction->getReturnType()) {
        llvm::Type* returnType = parentFunction->getReturnType();
        if (returnType->isFloatTy()) {
            valueType = llvm::Type::getFloatTy(context);
            defaultValue = llvm::ConstantFP::get(valueType, 0.0);
        } else if (returnType->isIntegerTy(32)) {
            valueType = llvm::Type::getInt32Ty(context);
            defaultValue = llvm::ConstantInt::get(valueType, 0);
        } else if (returnType->isIntegerTy(1)) {
            valueType = llvm::Type::getInt1Ty(context);
            defaultValue = llvm::ConstantInt::get(valueType, 0);
        }
    }

    // Create a PHI node to track the last value from the loop
    llvm::AllocaInst* lastValuePtr = builder->CreateAlloca(
        valueType, nullptr, "while.lastvalue");
    builder->CreateStore(defaultValue, lastValuePtr);

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
    } else if (condValue->getType()->isFloatTy()) {
        // Float type - compare with zero
        condBool = builder->CreateFCmpONE(condValue, llvm::ConstantFP::get(condValue->getType(), 0.0), "whilecond");
    } else {
        // For other types, convert to i32 first, then compare
        if (condValue->getType()->isDoubleTy()) {
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
    
    // Store the body value as the last value, converting type if necessary
    if (bodyValue->getType() == valueType) {
        builder->CreateStore(bodyValue, lastValuePtr);
    } else if (bodyValue->getType()->isIntegerTy() && valueType->isFloatTy()) {
        // Convert integer to float
        llvm::Value* convertedValue = builder->CreateSIToFP(bodyValue, valueType, "int_to_float");
        builder->CreateStore(convertedValue, lastValuePtr);
    } else if (bodyValue->getType()->isFloatTy() && valueType->isIntegerTy()) {
        // Convert float to integer
        llvm::Value* convertedValue = builder->CreateFPToSI(bodyValue, valueType, "float_to_int");
        builder->CreateStore(convertedValue, lastValuePtr);
    } else {
        // For other type mismatches, try to store the value directly or convert appropriately
        std::cout << "[DEBUG] WhileExpression: Type mismatch - bodyValue type: ";
        bodyValue->getType()->print(llvm::errs());
        std::cout << ", expected type: ";
        valueType->print(llvm::errs());
        std::cout << std::endl;
        
        // Try to store the value anyway - LLVM will handle basic conversions
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
        valueType, 
        lastValuePtr, 
        "while.result");
    
    std::cout << "[LOG] WhileExpression::codegen END, returning last value\n";
    
    return result;
}
