#include "type_system.hpp"
#include "tree.hpp"
#include <iostream>

const Type* TypeChecker::inferType(Expression* expr, IContext* context) {
    if (!expr) return nullptr;
    
    // Number literals
    if (dynamic_cast<Number*>(expr)) {
        return registry.getNumberType();
    }
    
    // String literals
    if (dynamic_cast<StringLiteral*>(expr)) {
        return registry.getStringType();
    }
    
    // Variables
    if (auto variable = dynamic_cast<Variable*>(expr)) {
        // First check if the variable has a type annotation stored in context
        if (context) {
            llvm::Type* llvmType = context->getVariableType(variable->getName());
            if (llvmType) {
                // Convert LLVM type back to our type system
                if (llvmType->isFloatTy()) {
                    return registry.getNumberType();
                } else if (llvmType->isPointerTy()) {
                    return registry.getStringType();
                } else if (llvmType->isIntegerTy(1)) {
                    return registry.getBooleanType();
                }
            }
        }
        
        // If no type annotation, try to infer from context
        // For now, default to Number if variable is defined
        if (context && context->isDefined(variable->getName())) {
            return registry.getNumberType(); // Default assumption
        }
        
        return nullptr; // Variable not found
    }
    
    // Binary operations
    if (auto binOp = dynamic_cast<BinaryOperation*>(expr)) {
        // For arithmetic operations, both operands should be Number
        std::string op = binOp->getOperation();
        if (op == "+" || op == "-" || op == "*" || op == "/" || op == "^" || op == "%") {
            return registry.getNumberType();
        }
        // For comparison operations, result is Boolean
        else if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
            return registry.getBooleanType();
        }
        // For logical operations, result is Boolean
        else if (op == "&&" || op == "||") {
            return registry.getBooleanType();
        }
        // For string concatenation, result is String
        else if (op == "@" || op == "@@") {
            return registry.getStringType();
        }
    }
    
    // Function calls
    if (dynamic_cast<FunctionCall*>(expr)) {
        // For now, assume function calls return Number
        // This should be improved to look up actual function signatures
        return registry.getNumberType();
    }
    
    // Method calls
    if (auto methodCall = dynamic_cast<MethodCall*>(expr)) {
        std::string methodName = methodCall->getMethodName();
        
        // Get the object's variable to determine its type
        if (auto variable = dynamic_cast<Variable*>(methodCall->getObject())) {
            std::string objectName = variable->getName();
            
            // Get the LLVM type of the variable from context
            if (context) {
                llvm::Type* llvmType = context->getVariableType(objectName);
                if (llvmType && llvmType->isPointerTy()) {
                    llvm::Type* pointedType = llvmType->getPointerElementType();
                    if (pointedType->isStructTy()) {
                        llvm::StructType* structType = llvm::cast<llvm::StructType>(pointedType);
                        std::string structName = structType->getName().str();
                        
                        // Remove "struct." prefix if present
                        if (structName.find("struct.") == 0) {
                            structName = structName.substr(7);
                        }
                        
                        // Now we have the actual type name, use it to analyze the method
                        const Type* returnType = inferMethodReturnType(structName, methodName, context);
                        if (returnType) {
                            return returnType;
                        }
                    }
                }
            }
        }
        
        // Default fallback
        return registry.getNumberType();
    }
    
    // Member access (e.g., self.x, obj.field)
    if (auto memberAccess = dynamic_cast<MemberAccess*>(expr)) {
        // Get the type of the object (left side of the dot)
        const Type* objectType = inferType(memberAccess->getObject(), context);
        if (objectType) {
            // If it's a user-defined type, look up the field type
            if (auto userType = dynamic_cast<const UserDefinedType*>(objectType)) {
                const Type* fieldType = userType->getFieldType(memberAccess->getMember());
                if (fieldType) {
                    return fieldType;
                }
            }
        }
        
        // For now, assume fields are Number type (based on x = 0, y = 0 initializers)
        // This is a fallback until we have proper field type tracking
        return registry.getNumberType();
    }
    
    // Print expressions
    if (dynamic_cast<Print*>(expr)) {
        return registry.getVoidType();
    }
    
    // Assignment expressions
    if (auto assign = dynamic_cast<AssignmentExpression*>(expr)) {
        // Assignment returns the type of the assigned value
        return inferType(assign->getExpression(), context);
    }
    
    // If expressions
    if (auto ifExpr = dynamic_cast<IfExpression*>(expr)) {
        // The type of an if expression is the type of its branches
        // Both branches should have the same type
        const Type* thenType = inferType(ifExpr->thenExpr, context);
        const Type* elseType = ifExpr->elseExpr ? inferType(ifExpr->elseExpr, context) : registry.getVoidType();
        
        if (areTypesCompatible(thenType, elseType)) {
            return thenType;
        }
        return nullptr; // Type mismatch
    }
    
    // Let-in expressions
    if (auto letIn = dynamic_cast<LetIn*>(expr)) {
        // The type of a let-in expression is the type of its body
        return inferType(letIn->getInExpression(), context);
    }
    
    // Block expressions
    if (auto block = dynamic_cast<BlockExpression*>(expr)) {
        // The type of a block is the type of its last statement
        const auto& statements = block->getStatements();
        if (!statements.empty()) {
            if (auto exprStmt = dynamic_cast<ExpressionStatement*>(statements.back())) {
                return inferType(exprStmt->getExpression(), context);
            }
        }
        return registry.getVoidType();
    }
    
    // Base expressions (base() calls)
    if (auto baseExpr = dynamic_cast<BaseExpression*>(expr)) {
        // A base() call returns the same type as the current method in the parent class
        // For now, we'll assume it returns String type since it's used in string concatenation
        // In a more sophisticated implementation, we'd look up the parent method's return type
        return registry.getStringType();
    }
    
    // Default: unknown type
    return nullptr;
}

const Type* TypeChecker::inferMethodReturnType(const std::string& typeName, const std::string& methodName, IContext* context) {
    if (!context) return nullptr;
    
    // Get the type definition from context
    TypeDefinition* typeDef = context->getTypeDefinition(typeName);
    if (!typeDef) return nullptr;
    
    // Find the method in the type definition
    const auto& methods = typeDef->getMethods();
    for (const auto& method : methods) {
        if (method.first == methodName) {
            // Found the method, analyze its body
            Expression* methodBody = method.second.second;
            if (methodBody) {
                // Create a temporary context for method analysis
                IContext* methodContext = context->createChildContext();
                
                // Add 'self' to the context
                methodContext->addVariable("self");
                
                // Add method parameters to the context
                const auto& params = method.second.first;
                for (const auto& param : params) {
                    methodContext->addVariable(param);
                }
                
                // Infer the type of the method body
                const Type* returnType = inferType(methodBody, methodContext);
                
                delete methodContext;
                return returnType;
            }
        }
    }
    
    // Method not found or no body
    return nullptr;
}