#include "tree.hpp"
#include "type_system.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

// AssignmentExpression implementation
AssignmentExpression::AssignmentExpression(const std::string& id, Expression* expr)
    : Expression(SourceLocation()), id(id), expr(expr), memberAccess(nullptr) {}

AssignmentExpression::AssignmentExpression(const SourceLocation& loc, const std::string& id, Expression* expr)
    : Expression(loc), id(id), expr(expr), memberAccess(nullptr) {}

AssignmentExpression::AssignmentExpression(const SourceLocation& loc, MemberAccess* memberAccess, Expression* expr)
    : Expression(loc), id(""), expr(expr), memberAccess(memberAccess) {}

std::string AssignmentExpression::toString() {
    if (memberAccess) {
        return memberAccess->toString() + " := " + expr->toString();
    } else {
        return id + " := " + expr->toString();
    }
}

void AssignmentExpression::printNode(int depth) {
    printIndent(depth);
    if (memberAccess) {
        std::cout << "├── Assignment Expression: (member access) := \n";
        printIndent(depth + 1);
        std::cout << "├── Member Access:\n";
        memberAccess->printNode(depth + 2);
    } else {
        std::cout << "├── Assignment Expression: " << id << " := \n";
    }
    if (expr) expr->printNode(depth + 1);
}

bool AssignmentExpression::Validate(IContext* context) {
    bool hasErrors = false;
    
    // Check if trying to assign to 'self' - this is not allowed
    if (!memberAccess && id == "self") {
        SEMANTIC_ERROR("Cannot assign to 'self': 'self' is not a valid assignment target", location);
        hasErrors = true;
    }
    
    if (!expr->Validate(context)) {
        if (memberAccess) {
            SEMANTIC_ERROR("Error in assignment expression to member", location);
        } else {
            SEMANTIC_ERROR("Error in assignment expression to variable '" + id + "'", location);
        }
        hasErrors = true;
    }
    
    // Create type registry and checker for strict type verification
    TypeRegistry typeRegistry;
    TypeChecker checker(typeRegistry);
    
    // Infer the type of the expression being assigned
    const Type* exprType = checker.inferType(expr, context);
    if (!exprType) {
        SEMANTIC_ERROR("Cannot infer type for expression in assignment", location);
        hasErrors = true;
    }
    
    if (exprType) {
        std::cout << "[DEBUG] AssignmentExpression::Validate - Expression type: " << exprType->toString() << std::endl;
    }
    
    if (memberAccess) {
        // Validate the member access first
        if (!memberAccess->Validate(context)) {
            hasErrors = true;
        }
        
        // Verify type of member field
        const Type* memberType = inferMemberType(memberAccess, context, typeRegistry);
        if (!memberType) {
            SEMANTIC_ERROR("Cannot determine type of member in assignment", location);
            hasErrors = true;
        } else {
            std::cout << "[DEBUG] AssignmentExpression::Validate - Member type: " << memberType->toString() << std::endl;
            
            if (exprType && !checker.areTypesCompatible(memberType, exprType)) {
                SEMANTIC_ERROR("Type mismatch in member assignment: expected " + 
                             memberType->toString() + ", got " + exprType->toString(), location);
                hasErrors = true;
            }
        }
        
        if (!hasErrors) {
            std::cout << "[DEBUG] Member assignment type check passed" << std::endl;
        }
    } else {
        // Verify type of variable
        llvm::Type* varLLVMType = context->getVariableType(id);
        if (!varLLVMType) {
            // Variable doesn't exist yet - this might be a new variable declaration
            // In this case, we allow the assignment and the variable will take the type of the expression
            std::cout << "[DEBUG] Variable '" << id << "' not found in context, allowing assignment (new variable)" << std::endl;
        } else {
            const Type* expectedType = llvmTypeToOurType(varLLVMType, typeRegistry);
            if (!expectedType) {
                SEMANTIC_ERROR("Cannot convert LLVM type to our type system for variable '" + id + "'", location);
                hasErrors = true;
            } else {
                std::cout << "[DEBUG] AssignmentExpression::Validate - Variable '" << id << "' expected type: " << expectedType->toString() << std::endl;
                
                if (exprType && !checker.areTypesCompatible(expectedType, exprType)) {
                    SEMANTIC_ERROR("Type mismatch in variable assignment: variable '" + id + 
                                 "' expects " + expectedType->toString() + ", got " + exprType->toString(), location);
                    hasErrors = true;
                }
            }
        }
        
        if (!hasErrors) {
            std::cout << "[DEBUG] Variable assignment type check passed" << std::endl;
        }
    }
    
    return !hasErrors;
}

AssignmentExpression::~AssignmentExpression() {
    delete expr;
    if (memberAccess) {
        delete memberAccess;
    }
}

llvm::Value* AssignmentExpression::codegen(CodeGenerator& generator) {
    // 1. Generate value of the expression
    llvm::Value* val = expr->codegen(generator);
    if (!val) return nullptr;

    if (memberAccess) {
        // Handle member access assignment
        llvm::Value* memberPtr = memberAccess->codegen(generator);
        if (!memberPtr) {
            SEMANTIC_ERROR("Failed to generate code for member access", getLocation());
            return nullptr;
        }
        
        // Check if the types are compatible
        llvm::Type* memberType = memberPtr->getType()->getPointerElementType();
        if (val->getType() != memberType) {
            // Try to cast if needed
            if (memberType->isIntegerTy() && val->getType()->isIntegerTy()) {
                val = generator.getBuilder()->CreateIntCast(val, memberType, true);
            } else {
                SEMANTIC_ERROR("Type mismatch in member assignment", getLocation());
                return nullptr;
            }
        }
        
        // Store the value to the member
        generator.getBuilder()->CreateStore(val, memberPtr);
        return val;
    } else {
        // Handle regular variable assignment
        // 2. Look up variable
        llvm::Value* variable = generator.getNamedValue(id);
        llvm::Type* varType = llvm::Type::getFloatTy(generator.getContext());

        if (!variable) {
            // If it doesn't exist, create new alloca
            llvm::Function* func = generator.getBuilder()->GetInsertBlock()->getParent();
            llvm::IRBuilder<> tmpBuilder(&func->getEntryBlock(), func->getEntryBlock().begin());
            llvm::AllocaInst* alloca = tmpBuilder.CreateAlloca(varType, nullptr, id);
            generator.setNamedValue(id, alloca);
            variable = alloca;
        }

        // 3. Store value
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(variable)) {
            if (val->getType() != alloca->getAllocatedType()) {
                SEMANTIC_ERROR("Type mismatch in assignment expression to variable '" + id + "'", getLocation());
                return nullptr;
            }
            generator.getBuilder()->CreateStore(val, alloca);
        } else {
            SEMANTIC_ERROR("Variable storage for '" + id + "' is not an alloca", getLocation());
            return nullptr;
        }
    }
    
    // 4. Return the value (this is what makes it an expression)
    return val;
}

// Helper function to infer the type of a member access
const Type* AssignmentExpression::inferMemberType(MemberAccess* memberAccess, IContext* context, TypeRegistry& typeRegistry) {
    // This is a simplified implementation
    // In a full system, you would need to:
    // 1. Get the type of the object being accessed
    // 2. Look up the field type in that object's type definition
    
    // For now, we'll return a default type based on common patterns
    // This should be replaced with proper member type lookup
    
    std::cout << "[DEBUG] inferMemberType called for member access" << std::endl;
    
    // Try to infer the object type first
    TypeChecker checker(typeRegistry);
    // Note: memberAccess->getObject() would need to be implemented in MemberAccess class
    // For now, we'll assume Number type for simplicity
    
    return typeRegistry.getNumberType(); // Default assumption
}

// Helper function to convert LLVM type to our type system
const Type* AssignmentExpression::llvmTypeToOurType(llvm::Type* llvmType, TypeRegistry& typeRegistry) {
    if (!llvmType) return nullptr;
    
    if (llvmType->isFloatTy()) {
        return typeRegistry.getNumberType();
    } else if (llvmType->isIntegerTy(32)) {
        return typeRegistry.getNumberType(); // Treat i32 as Number
    } else if (llvmType->isIntegerTy(1)) {
        return typeRegistry.getBooleanType();
    } else if (llvmType->isPointerTy()) {
        llvm::Type* pointeeType = llvmType->getPointerElementType();
        if (pointeeType->isIntegerTy(8)) {
            return typeRegistry.getStringType(); // i8* is String
        }
        // For other pointer types, we might need more sophisticated handling
        return typeRegistry.getNumberType(); // Default
    } else if (llvmType->isVoidTy()) {
        return typeRegistry.getVoidType();
    }
    
    // Default to Number for unknown types
    return typeRegistry.getNumberType();
}