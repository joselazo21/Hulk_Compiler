#include "type_system.hpp"
#include "tree.hpp"
#include <iostream>

const Type* TypeRegistry::getType(const std::string& name) const {
    auto it = types.find(name);
    if (it != types.end()) {
        return it->second.get();
    }
    
    // If not found and we have a context, try to create the type dynamically
    if (context && context->hasType(name)) {
        // Create a UserDefinedType for this custom type
        auto userType = std::make_unique<UserDefinedType>(name);
        const Type* result = userType.get();
        
        // Cast away const to register the type (this is a design compromise)
        const_cast<TypeRegistry*>(this)->registerType(name, std::move(userType));
        return result;
    }
    
    return nullptr;
}


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
        // First check if the variable has a semantic type name stored in context
        if (context) {
            std::string typeName = context->getVariableTypeName(variable->getName());
            if (!typeName.empty()) {
                // Return the semantic type
                if (typeName == "Number") {
                    return registry.getNumberType();
                } else if (typeName == "String") {
                    return registry.getStringType();
                } else if (typeName == "Boolean") {
                    return registry.getBooleanType();
                } else {
                    // For custom types, get from registry
                    return registry.getType(typeName);
                }
            }
            
            // Fallback: check LLVM type
            llvm::Type* llvmType = context->getVariableType(variable->getName());
            if (llvmType) {
                // Convert LLVM type back to our type system
                if (llvmType->isFloatTy()) {
                    return registry.getNumberType();
                } else if (llvmType->isPointerTy()) {
                    // Check if it's a struct pointer (custom type)
                    llvm::Type* pointedType = llvmType->getPointerElementType();
                    if (pointedType->isStructTy()) {
                        llvm::StructType* structType = llvm::cast<llvm::StructType>(pointedType);
                        std::string structName = structType->getName().str();
                        // Remove "struct." prefix if present
                        if (structName.find("struct.") == 0) {
                            structName = structName.substr(7);
                        }
                        return registry.getType(structName);
                    }
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
    
    // Unary operations
    if (auto unaryOp = dynamic_cast<UnaryOperation*>(expr)) {
        std::string op = unaryOp->getOperation();
        if (op == "-") {
            // Unary minus on numbers returns Number
            const Type* operandType = inferType(unaryOp->getOperand(), context);
            if (operandType && operandType == registry.getNumberType()) {
                return registry.getNumberType();
            }
        }
        // Add support for other unary operators as needed
        return nullptr; // Unsupported unary operation
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
    if (auto functionCall = dynamic_cast<FunctionCall*>(expr)) {
        std::string funcName = functionCall->getFunctionName();
        
        // Try to get function return type from context first
        if (context) {
            std::string returnType = context->getFunctionReturnType(funcName);
            if (!returnType.empty()) {
                if (returnType == "Number") {
                    return registry.getNumberType();
                } else if (returnType == "String") {
                    return registry.getStringType();
                } else if (returnType == "Boolean") {
                    return registry.getBooleanType();
                } else {
                    // For custom types, check if they exist in the context
                    std::cout << "[DEBUG] TypeChecker::inferType - Checking custom type '" << returnType << "'" << std::endl;
                    if (context && context->hasType(returnType)) {
                        std::cout << "[DEBUG] TypeChecker::inferType - Type '" << returnType << "' exists in context" << std::endl;
                        // Try to get the type from registry first
                        const Type* customType = registry.getType(returnType);
                        if (customType) {
                            std::cout << "[DEBUG] TypeChecker::inferType - Found type '" << returnType << "' in registry" << std::endl;
                            return customType;
                        }
                        std::cout << "[DEBUG] TypeChecker::inferType - Type '" << returnType << "' not in registry, creating dynamically" << std::endl;
                        // If not in registry but exists in context, create it dynamically
                        // The registry's getType method should handle this via dynamic creation
                        const Type* dynamicType = registry.getType(returnType);
                        if (dynamicType) {
                            std::cout << "[DEBUG] TypeChecker::inferType - Successfully created dynamic type '" << returnType << "'" << std::endl;
                            return dynamicType;
                        } else {
                            std::cout << "[DEBUG] TypeChecker::inferType - Failed to create dynamic type '" << returnType << "'" << std::endl;
                        }
                    } else {
                        std::cout << "[DEBUG] TypeChecker::inferType - Type '" << returnType << "' not found in context" << std::endl;
                    }
                    // If not found, default to Number
                    std::cout << "[DEBUG] TypeChecker::inferType - Defaulting to Number for type '" << returnType << "'" << std::endl;
                    return registry.getNumberType();
                }
            }
        }
        
        // Fallback: Handle built-in functions with known return types
        if (funcName == "random" || funcName == "rand" || funcName == "sqrt" || 
            funcName == "sin" || funcName == "cos" || funcName == "pow" || 
            funcName == "print" || funcName == "range") {
            return registry.getNumberType();
        }
        
        // Default to Number for unknown functions
        return registry.getNumberType();
    }
    
    // Method calls
    if (auto methodCall = dynamic_cast<MethodCall*>(expr)) {
        std::string methodName = methodCall->getMethodName();
        
        // Get the object's type to determine the method return type
        std::string objectTypeName;
        
        // Handle Variable objects (e.g., obj.method())
        if (auto variable = dynamic_cast<Variable*>(methodCall->getObject())) {
            std::string objectName = variable->getName();
            
            // Get the LLVM type of the variable from context
            if (context) {
                llvm::Type* llvmType = context->getVariableType(objectName);
                if (llvmType && llvmType->isPointerTy()) {
                    llvm::Type* pointedType = llvmType->getPointerElementType();
                    if (pointedType->isStructTy()) {
                        llvm::StructType* structType = llvm::cast<llvm::StructType>(pointedType);
                        objectTypeName = structType->getName().str();
                        
                        // Remove "struct." prefix if present
                        if (objectTypeName.find("struct.") == 0) {
                            objectTypeName = objectTypeName.substr(7);
                        }
                    }
                }
            }
        }
        // Handle SelfExpression objects (e.g., self.method())
        else if (dynamic_cast<SelfExpression*>(methodCall->getObject())) {
            // Get the current type being processed from context
            objectTypeName = context->getCurrentType();
        }
        
        // If we have a type name, use it to analyze the method
        if (!objectTypeName.empty()) {
            const Type* returnType = inferMethodReturnType(objectTypeName, methodName, context);
            if (returnType) {
                return returnType;
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
            
            // For custom types, we need to infer field types based on the type definition
            std::string typeName = objectType->toString();
            if (context) {
                TypeDefinition* typeDef = context->getTypeDefinition(typeName);
                if (typeDef) {
                    // Get the fields from the type definition
                    const auto& fields = typeDef->getFields();
                    std::string memberName = memberAccess->getMember();
                    
                    // Look for the field in the type definition
                    for (const auto& field : fields) {
                        if (field.first == memberName) {
                            // Found the field, now infer its type from the initialization expression
                            Expression* initExpr = field.second;
                            if (initExpr) {
                                // If it's a NewExpression, return the type being instantiated
                                if (auto newExpr = dynamic_cast<NewExpression*>(initExpr)) {
                                    return registry.getType(newExpr->getTypeName());
                                }
                                // For other expressions, recursively infer the type
                                const Type* fieldType = inferType(initExpr, context);
                                if (fieldType) {
                                    return fieldType;
                                }
                            }
                            // If no initialization expression, assume Number (common default)
                            return registry.getNumberType();
                        }
                    }
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

        Expression* inExpr = letIn->getInExpression();

        const Type* resultType = inferType(inExpr, context);

        return resultType;
    }
    
    // Block expressions
    if (auto block = dynamic_cast<BlockExpression*>(expr)) {
        // The type of a block is the type of its last statement

        const auto& statements = block->getStatements();

        if (!statements.empty()) {
            // Check from the last statement backwards
            for (auto it = statements.rbegin(); it != statements.rend(); ++it) {
                Statement* stmt = *it;

                
                // If it's an expression statement, return its type
                if (auto exprStmt = dynamic_cast<ExpressionStatement*>(stmt)) {

                    const Type* resultType = inferType(exprStmt->getExpression(), context);

                    return resultType;
                }
                
                // Skip function declarations as they don't contribute to the return value
                if (dynamic_cast<FunctionDeclaration*>(stmt)) {

                    continue;
                }
                
                // For ForStatement, analyze its body to determine return type
                if (auto forStmt = dynamic_cast<ForStatement*>(stmt)) {

                    // Get the last expression from the for loop body
                    BlockStatement* forBody = forStmt->getBody();
                    if (forBody) {

                        Expression* lastExpr = forBody->getLastExpression();
                        if (lastExpr) {

                            const Type* resultType = inferType(lastExpr, context);

                            return resultType;
                        } else {

                        }
                    } else {

                    }
                    // If no expression found in for body, continue looking
                    continue;
                }
                
                // For WhileStatement, analyze its body to determine return type
                if (auto whileStmt = dynamic_cast<WhileStatement*>(stmt)) {

                    // Get the last expression from the while loop body
                    BlockStatement* whileBody = whileStmt->getBody();
                    if (whileBody) {

                        Expression* lastExpr = whileBody->getLastExpression();
                        if (lastExpr) {

                            const Type* resultType = inferType(lastExpr, context);

                            return resultType;
                        } else {

                        }
                    } else {

                    }
                    // If no expression found in while body, continue looking
                    continue;
                }
                
                // Let's check what type of statement this actually is
                if (dynamic_cast<Assignment*>(stmt)) {

                } else if (dynamic_cast<IfStatement*>(stmt)) {

                } else if (dynamic_cast<WhileStatement*>(stmt)) {

                } else if (dynamic_cast<ReturnStatement*>(stmt)) {

                } else {

                }

                // For other statement types that don't contribute to return value
                // we continue looking backwards
            }
        }

        return registry.getVoidType();
    }
    
    // New expressions (object instantiation)
    if (auto newExpr = dynamic_cast<NewExpression*>(expr)) {
        std::string typeName = newExpr->getTypeName();
        // Return the type being instantiated
        const Type* customType = registry.getType(typeName);
        if (customType) {
            return customType;
        }
        // If type not found in registry, it might be a custom type
        // Try to get it from context
        if (context && context->hasType(typeName)) {
            // The registry should create it dynamically via getType()
            return registry.getType(typeName);
        }
        return nullptr;
    }
    
    // Base expressions (base() calls)
    if (dynamic_cast<BaseExpression*>(expr)) {
        // A base() call returns the same type as the current method in the parent class
        // For now, we'll assume it returns String type since it's used in string concatenation
        // In a more sophisticated implementation, we'd look up the parent method's return type
        return registry.getStringType();
    }
    
    // Default: unknown type
    return nullptr;
}

const Type* TypeChecker::inferMethodReturnType(const std::string& typeName, const std::string& methodName, IContext* context) {

    
    if (!context) {

        return nullptr;
    }
    
    // Get the type definition from context
    TypeDefinition* typeDef = context->getTypeDefinition(typeName);
    if (!typeDef) {

        return nullptr;
    }
    

    
    // Check typed methods first (methods with return type annotations)
    if (typeDef->getUseTypedMethods()) {
        const auto& typedMethods = typeDef->getTypedMethods();

        
        for (const auto& method : typedMethods) {

            if (method.first == methodName) {

                // Found the method, check if it has a return type annotation
                const std::string& returnTypeAnnotation = method.second.returnType;

                
                if (!returnTypeAnnotation.empty()) {
                    // Return the annotated type directly
                    if (returnTypeAnnotation == "String") {

                        return registry.getStringType();
                    } else if (returnTypeAnnotation == "Number") {

                        return registry.getNumberType();
                    } else if (returnTypeAnnotation == "Boolean") {

                        return registry.getBooleanType();
                    } else {
                        // For user-defined types, default to Number for now

                        return registry.getNumberType();
                    }
                }
                
                // If no return type annotation, analyze the body
                Expression* methodBody = method.second.body;
                if (methodBody) {
                    // Create a temporary context for method analysis
                    IContext* methodContext = context->createChildContext();
                    
                    // Add 'self' to the context
                    methodContext->addVariable("self");
                    
                    // Add method parameters to the context
                    const auto& params = method.second.params;
                    for (const auto& param : params) {
                        // Extract just the parameter name if it contains type annotation
                        std::string paramName = param;
                        size_t colonPos = paramName.find(':');
                        if (colonPos != std::string::npos) {
                            paramName = paramName.substr(0, colonPos);
                            // Remove any trailing whitespace
                            paramName.erase(paramName.find_last_not_of(" \t") + 1);
                        }
                        methodContext->addVariable(paramName);
                    }
                    
                    // Infer the type of the method body
                    const Type* returnType = inferType(methodBody, methodContext);
                    
                    delete methodContext;
                    return returnType;
                }
            }
        }
    } else {
        // Fall back to legacy methods
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
                        // Extract just the parameter name if it contains type annotation
                        std::string paramName = param;
                        size_t colonPos = paramName.find(':');
                        if (colonPos != std::string::npos) {
                            paramName = paramName.substr(0, colonPos);
                            // Remove any trailing whitespace
                            paramName.erase(paramName.find_last_not_of(" \t") + 1);
                        }
                        methodContext->addVariable(paramName);
                    }
                    
                    // Infer the type of the method body
                    const Type* returnType = inferType(methodBody, methodContext);
                    
                    delete methodContext;
                    return returnType;
                }
            }
        }
    }
    
    // Method not found or no body
    return nullptr;
}