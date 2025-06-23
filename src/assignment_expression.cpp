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

            
            if (exprType && !checker.areTypesCompatible(memberType, exprType)) {
                SEMANTIC_ERROR("Type mismatch in member assignment: expected " + 
                             memberType->toString() + ", got " + exprType->toString(), location);
                hasErrors = true;
            }
        }
        
        if (!hasErrors) {

        }
    } else {
        // Verify type of variable
        llvm::Type* varLLVMType = context->getVariableType(id);
        if (!varLLVMType) {
            // Variable doesn't exist yet - this might be a new variable declaration
            // In this case, we allow the assignment and the variable will take the type of the expression

        } else {
            const Type* expectedType = llvmTypeToOurType(varLLVMType, typeRegistry);
            if (!expectedType) {
                SEMANTIC_ERROR("Cannot convert LLVM type to our type system for variable '" + id + "'", location);
                hasErrors = true;
            } else {

                
                if (exprType && !checker.areTypesCompatible(expectedType, exprType)) {
                    SEMANTIC_ERROR("Type mismatch in variable assignment: variable '" + id + 
                                 "' expects " + expectedType->toString() + ", got " + exprType->toString(), location);
                    hasErrors = true;
                }
            }
        }
        
        if (!hasErrors) {

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
        llvm::Value* memberPtr = memberAccess->getFieldPointer(generator);
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
            } else if (memberType->isFloatTy() && val->getType()->isFloatTy()) {
                // Both are float types, should be compatible
                // No casting needed
            } else {
                SEMANTIC_ERROR("Type mismatch in member assignment", getLocation());
                return nullptr;
            }
        }
        
        // Store the value to the member
        generator.getBuilder()->CreateStore(val, memberPtr);
        
        // For member assignments, return the assigned value (not the pointer)
        // This is important for setter methods that should return the assigned value
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

    
    // Get the object being accessed (e.g., 'self' in 'self.data')
    Expression* object = memberAccess->getObject();
    std::string memberName = memberAccess->getMember();
    
    // Check if it's a self reference
    if (auto selfExpr = dynamic_cast<SelfExpression*>(object)) {
        // Get the current type being processed
        std::string currentTypeName = context->getCurrentType();
        if (!currentTypeName.empty()) {

            
            // Get the type definition
            TypeDefinition* typeDef = context->getTypeDefinition(currentTypeName);
            if (typeDef) {

                
                // Look through the fields to find the member
                const auto& fields = typeDef->getFields();
                for (const auto& field : fields) {
                    std::string fieldName = field.first;
                    
                    // Extract the actual field name and type from the stored format
                    std::string actualFieldName = fieldName;
                    std::string fieldTypeName = "Number"; // Default
                    
                    size_t colonPos = fieldName.find(':');
                    if (colonPos != std::string::npos) {
                        actualFieldName = fieldName.substr(0, colonPos);
                        fieldTypeName = fieldName.substr(colonPos + 1);
                        // Remove any leading/trailing whitespace
                        fieldTypeName.erase(0, fieldTypeName.find_first_not_of(" \t"));
                        fieldTypeName.erase(fieldTypeName.find_last_not_of(" \t") + 1);
                    }
                
                    if (actualFieldName == memberName) {

                        
                        // Return the appropriate type based on the field type annotation
                        if (fieldTypeName == "String") {
                            return typeRegistry.getStringType();
                        } else if (fieldTypeName == "Boolean") {
                            return typeRegistry.getBooleanType();
                        } else {
                            return typeRegistry.getNumberType();
                        }
                    }
                }
            }
        }
    }
    
    // If we can't determine the type, fall back to Number

    return typeRegistry.getNumberType();
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