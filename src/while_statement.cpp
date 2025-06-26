#include "tree.hpp"
#include "type_system.hpp"
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
    bool hasErrors = false;
    
    // First validate the condition expression itself
    if (!condition->Validate(context)) {
        SEMANTIC_ERROR("Error in while condition", location);
        hasErrors = true;
    }
    
    // Now check if the condition is of boolean type
    TypeRegistry typeRegistry(context);
    TypeChecker typeChecker(typeRegistry);
    
    const Type* conditionType = typeChecker.inferType(condition, context);
    if (conditionType) {
        const Type* booleanType = typeRegistry.getBooleanType();
        
        // Check if the condition type is compatible with boolean
        if (!typeChecker.areTypesCompatible(booleanType, conditionType)) {
            // Special case: allow numeric types that can be converted to boolean
            const Type* numberType = typeRegistry.getNumberType();
            if (!typeChecker.areTypesCompatible(numberType, conditionType)) {
                SEMANTIC_ERROR("While condition must be of boolean type, got '" + 
                             conditionType->toString() + "'", location);
                hasErrors = true;
            }
        }
    } else {
        SEMANTIC_ERROR("Cannot determine type of while condition", location);
        hasErrors = true;
    }
    
    // Validate the body
    if (!body->Validate(context)) {
        SEMANTIC_ERROR("Error in while loop body", location);
        hasErrors = true;
    }
    
    return !hasErrors;
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

    // Don't pre-allocate result pointer - we'll create it dynamically based on the actual value type
    llvm::Value* resultPtr = nullptr;

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
            // Create result pointer with the correct type based on the actual value
            if (!resultPtr) {
                llvm::IRBuilder<> tmpBuilder(&func->getEntryBlock(), func->getEntryBlock().begin());
                resultPtr = tmpBuilder.CreateAlloca(lastValue->getType(), nullptr, "while.result");
            }
            generator.getBuilder()->CreateStore(lastValue, resultPtr);
        }
    }

    // Only add a branch back to the condition if the current block is not already terminated.
    // This is crucial because the body of the loop might contain its own control flow,
    // such as a return or break statement, which would already terminate the block.
    if (generator.getBuilder()->GetInsertBlock()->getTerminator() == nullptr) {
        generator.getBuilder()->CreateBr(condBB);
    }

    generator.getBuilder()->SetInsertPoint(endBB);
    
    // The end block should not have a terminator - it should fall through
    // to whatever comes next in the calling code
    if (resultPtr) {
        // If we have a result from the loop body, load and return it
        llvm::Value* result = generator.getBuilder()->CreateLoad(
            resultPtr->getType()->getPointerElementType(), resultPtr, "while.final.result");
        return result;
    }
    
    // Return a default value - the calling code will handle adding terminators
    return llvm::ConstantFP::get(llvm::Type::getFloatTy(generator.getContext()), 0.0f);
}