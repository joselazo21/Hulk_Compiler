#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>


IfExpression::IfExpression(Expression* cond, Expression* thenExpr, Expression* elseExpr)
    : Expression(SourceLocation()), condition(cond), thenExpr(thenExpr), elseExpr(elseExpr) {}

IfExpression::IfExpression(const SourceLocation& loc, Expression* cond, Expression* thenExpr, Expression* elseExpr)
    : Expression(loc), condition(cond), thenExpr(thenExpr), elseExpr(elseExpr) {}

IfExpression::~IfExpression() {
    if (condition) delete condition;
    if (thenExpr) delete thenExpr;
    if (elseExpr) delete elseExpr;
}

std::string IfExpression::toString() {
    std::ostringstream oss;
    oss << "if (" << condition->toString() << ") "
        << thenExpr->toString()
        << " else "
        << elseExpr->toString();
    return oss.str();
}

void IfExpression::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── If Expression\n";
    printIndent(depth);
    std::cout << "│   ├── Condition:\n";
    condition->printNode(depth + 2);
    printIndent(depth);
    std::cout << "│   ├── Then Expression:\n";
    thenExpr->printNode(depth + 2);
    printIndent(depth);
    std::cout << "│   └── Else Expression:\n";
    elseExpr->printNode(depth + 2);
}

bool IfExpression::Validate(IContext* context) {
    bool hasErrors = false;
    
    if (!condition->Validate(context)) {
        SEMANTIC_ERROR("Error in if-expression condition", location);
        hasErrors = true;
    }
    if (!thenExpr->Validate(context)) {
        SEMANTIC_ERROR("Error in then branch of if-expression", location);
        hasErrors = true;
    }
    if (!elseExpr->Validate(context)) {
        SEMANTIC_ERROR("Error in else branch of if-expression", location);
        hasErrors = true;
    }
    return !hasErrors;
}

llvm::Value* IfExpression::codegen(CodeGenerator& generator) {
    // 1. Generate condition
    llvm::Value* condVal = condition->codegen(generator);
    if (!condVal) return nullptr;

    // 2. Convert condition to boolean (i1)
    if (condVal->getType()->isIntegerTy(1)) {
        // Already a boolean, use as is
    } else if (condVal->getType()->isIntegerTy()) {
        // Integer type - compare with zero of the same type
        llvm::Type* condType = condVal->getType();
        condVal = generator.getBuilder()->CreateICmpNE(
            condVal, llvm::ConstantInt::get(condType, 0), "ifcond");
    } else {
        // For other types, convert to i32 first, then compare
        if (condVal->getType()->isDoubleTy() || condVal->getType()->isFloatTy()) {
            condVal = generator.getBuilder()->CreateFPToSI(condVal, llvm::Type::getInt32Ty(generator.getContext()), "fp_to_int");
        }
        condVal = generator.getBuilder()->CreateICmpNE(
            condVal, llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0), "ifcond");
    }

    // 3. Get current function
    llvm::Function* func = generator.getBuilder()->GetInsertBlock()->getParent();

    // 4. Create blocks for then, else, and merge
    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(generator.getContext(), "ifexpr.then", func);
    llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(generator.getContext(), "ifexpr.else");
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(generator.getContext(), "ifexpr.merge");

    // 5. Conditional branch
    generator.getBuilder()->CreateCondBr(condVal, thenBB, elseBB);

    // 6. Emit then block
    generator.getBuilder()->SetInsertPoint(thenBB);
    llvm::Value* thenVal = thenExpr->codegen(generator);
    if (!thenVal) return nullptr;
    
    // Store the then value and block before branching
    llvm::Value* thenValForPhi = thenVal;
    llvm::BasicBlock* thenBlockForPhi = generator.getBuilder()->GetInsertBlock();
    
    generator.getBuilder()->CreateBr(mergeBB);

    // 7. Emit else block
    func->getBasicBlockList().push_back(elseBB);
    generator.getBuilder()->SetInsertPoint(elseBB);
    llvm::Value* elseVal = elseExpr->codegen(generator);
    if (!elseVal) return nullptr;
    
    // Store the else value and block before branching
    llvm::Value* elseValForPhi = elseVal;
    llvm::BasicBlock* elseBlockForPhi = generator.getBuilder()->GetInsertBlock();
    
    generator.getBuilder()->CreateBr(mergeBB);

    // 8. Emit merge block
    func->getBasicBlockList().push_back(mergeBB);
    generator.getBuilder()->SetInsertPoint(mergeBB);

    // Ensure type consistency between branches
    llvm::Type* thenType = thenValForPhi->getType();
    llvm::Type* elseType = elseValForPhi->getType();

    llvm::Type* phiType = nullptr;

    if (thenType == elseType) {
        // Types already match, use as is
        phiType = thenType;
    } else if (thenType->isPointerTy() && elseType->isPointerTy()) {
        // Both are pointers - check if they're object pointers
        llvm::Type* thenPointedType = thenType->getPointerElementType();
        llvm::Type* elsePointedType = elseType->getPointerElementType();
        
        if (thenPointedType->isStructTy() && elsePointedType->isStructTy()) {
            // Both are object pointers - cast to common base type (i8*)
            phiType = llvm::Type::getInt8PtrTy(generator.getContext());
            
            // Perform casts in their respective blocks before the branch
            generator.getBuilder()->SetInsertPoint(thenBlockForPhi->getTerminator());
            thenValForPhi = generator.getBuilder()->CreateBitCast(thenValForPhi, phiType, "then.cast");
            
            generator.getBuilder()->SetInsertPoint(elseBlockForPhi->getTerminator());
            elseValForPhi = generator.getBuilder()->CreateBitCast(elseValForPhi, phiType, "else.cast");
            
            // Reset insert point to merge block
            generator.getBuilder()->SetInsertPoint(mergeBB);
        } else if (thenPointedType->isIntegerTy(8) && elsePointedType->isIntegerTy(8)) {
            // Both are string pointers
            phiType = thenType;
        } else {
            // Mixed pointer types - cast to void*
            phiType = llvm::Type::getInt8PtrTy(generator.getContext());
            
            // Perform casts in their respective blocks before the branch
            generator.getBuilder()->SetInsertPoint(thenBlockForPhi->getTerminator());
            thenValForPhi = generator.getBuilder()->CreateBitCast(thenValForPhi, phiType, "then.cast");
            
            generator.getBuilder()->SetInsertPoint(elseBlockForPhi->getTerminator());
            elseValForPhi = generator.getBuilder()->CreateBitCast(elseValForPhi, phiType, "else.cast");
            
            // Reset insert point to merge block
            generator.getBuilder()->SetInsertPoint(mergeBB);
        }
    } else if (thenType->isIntegerTy(32) && elseType->isPointerTy() && elseType->getPointerElementType()->isIntegerTy(8)) {
        // then: int, else: string -> convert int to string
        generator.getBuilder()->SetInsertPoint(thenBlockForPhi->getTerminator());
        thenValForPhi = generator.intToString(thenValForPhi);
        generator.getBuilder()->SetInsertPoint(mergeBB);
        phiType = elseType;
    } else if (elseType->isIntegerTy(32) && thenType->isPointerTy() && thenType->getPointerElementType()->isIntegerTy(8)) {
        // else: int, then: string -> convert int to string
        generator.getBuilder()->SetInsertPoint(elseBlockForPhi->getTerminator());
        elseValForPhi = generator.intToString(elseValForPhi);
        generator.getBuilder()->SetInsertPoint(mergeBB);
        phiType = thenType;
    } else {
        // Other cases: try to convert both to string
        phiType = llvm::Type::getInt8PtrTy(generator.getContext());
        
        if (thenType->isIntegerTy(32)) {
            generator.getBuilder()->SetInsertPoint(thenBlockForPhi->getTerminator());
            thenValForPhi = generator.intToString(thenValForPhi);
        }
        if (elseType->isIntegerTy(32)) {
            generator.getBuilder()->SetInsertPoint(elseBlockForPhi->getTerminator());
            elseValForPhi = generator.intToString(elseValForPhi);
        }
        
        // Reset insert point to merge block
        generator.getBuilder()->SetInsertPoint(mergeBB);
        
        // If they still don't match, cast both to void*
        if (thenValForPhi->getType() != phiType) {
            generator.getBuilder()->SetInsertPoint(thenBlockForPhi->getTerminator());
            thenValForPhi = generator.getBuilder()->CreateBitCast(thenValForPhi, phiType, "then.final.cast");
        }
        if (elseValForPhi->getType() != phiType) {
            generator.getBuilder()->SetInsertPoint(elseBlockForPhi->getTerminator());
            elseValForPhi = generator.getBuilder()->CreateBitCast(elseValForPhi, phiType, "else.final.cast");
        }
        
        // Reset insert point to merge block
        generator.getBuilder()->SetInsertPoint(mergeBB);
    }

    // Create PHI node with the unified type
    llvm::PHINode* phi = generator.getBuilder()->CreatePHI(phiType, 2, "iftmp");
    phi->addIncoming(thenValForPhi, thenBlockForPhi);
    phi->addIncoming(elseValForPhi, elseBlockForPhi);

    return phi;
}
