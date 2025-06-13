#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

IfStatement::IfStatement(Expression* cond, BlockStatement* then, BlockStatement* else_branch)
    : Statement(SourceLocation()), condition(cond), thenBranch(then), elseBranch(else_branch) {}

IfStatement::IfStatement(const SourceLocation& loc, Expression* cond, BlockStatement* then, BlockStatement* else_branch)
    : Statement(loc), condition(cond), thenBranch(then), elseBranch(else_branch) {}

void IfStatement::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── If Statement\n";
    printIndent(depth);
    std::cout << "│   ├── Condition:\n";
    condition->printNode(depth + 2);
    printIndent(depth);
    std::cout << "│   ├── Then Branch:\n";
    thenBranch->printNode(depth + 2);
    printIndent(depth);
    std::cout << "│   └── Else Branch:\n";
    elseBranch->printNode(depth + 2);
}

bool IfStatement::Validate(IContext* context) {
    if (!condition->Validate(context)) {
        SEMANTIC_ERROR("Error in if condition", location);
        return false;
    }
    if (!thenBranch->Validate(context)) {
        SEMANTIC_ERROR("Error in then branch of if statement", location);
        return false;
    }
    if (elseBranch && !elseBranch->Validate(context)) {
        SEMANTIC_ERROR("Error in else branch of if statement", location);
        return false;
    }
    return true;
}

IfStatement::~IfStatement() {
    delete condition;
    delete thenBranch;
    if (elseBranch) delete elseBranch;
}

llvm::Value* IfStatement::codegen(CodeGenerator& generator) {
    // 1. Generar condición
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
    
    // 3. Obtener función actual
    llvm::Function* func = generator.getBuilder()->GetInsertBlock()->getParent();
    
    // 4. Crear bloques básicos
    llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(generator.getContext(), "then", func);
    llvm::BasicBlock* elseBB = elseBranch ? 
        llvm::BasicBlock::Create(generator.getContext(), "else", func) : nullptr;
    llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(generator.getContext(), "ifcont", func);
    
    // 5. Crear bifurcación
    if (elseBB) {
        generator.getBuilder()->CreateCondBr(condVal, thenBB, elseBB);
    } else {
        generator.getBuilder()->CreateCondBr(condVal, thenBB, mergeBB);
    }
    
    // 6. Generar bloque then
    generator.getBuilder()->SetInsertPoint(thenBB);
    if (!thenBranch->codegen(generator)) {
        return nullptr;
    }
    generator.getBuilder()->CreateBr(mergeBB);
    
    // 7. Generar bloque else (si existe)
    if (elseBB) {
        generator.getBuilder()->SetInsertPoint(elseBB);
        if (!elseBranch->codegen(generator)) {
            return nullptr;
        }
        generator.getBuilder()->CreateBr(mergeBB);
    }
    
    // 8. Establecer punto de inserción al merge
    generator.getBuilder()->SetInsertPoint(mergeBB);
    
    return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(generator.getContext()));
}
