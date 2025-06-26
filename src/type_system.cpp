#include "type_system.hpp"
#include "tree.hpp"
#include <iostream>
#include <unordered_set>

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
    
    // Note: We removed the unsafe IfStatement cast handling here
    // Instead, we'll handle this case differently in getLastExpression()
    
    // Number literals
    if (dynamic_cast<Number*>(expr)) {
        return registry.getNumberType();
    }
    
    // String literals
    if (dynamic_cast<StringLiteral*>(expr)) {
        return registry.getStringType();
    }
    
    // Boolean literals
    if (dynamic_cast<Boolean*>(expr)) {
        return registry.getBooleanType();
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
    
    // Self expressions
    if (dynamic_cast<SelfExpression*>(expr)) {
        // Get the current type being processed from context
        if (context) {
            std::string currentType = context->getCurrentType();
            if (!currentType.empty()) {
                std::cout << "[DEBUG] TypeChecker::inferType - SelfExpression current type: " << currentType << std::endl;
                const Type* selfType = registry.getType(currentType);
                if (selfType) {
                    return selfType;
                }
                // If not in registry, try to create it dynamically
                return registry.getType(currentType);
            }
        }
        return nullptr; // No current type context
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
        std::cout << "[DEBUG] TypeChecker::inferType - Processing MemberAccess: " << memberAccess->getMember() << std::endl;
        
        // Get the type of the object (left side of the dot)
        const Type* objectType = inferType(memberAccess->getObject(), context);
        std::cout << "[DEBUG] TypeChecker::inferType - MemberAccess object type: " << (objectType ? objectType->toString() : "null") << std::endl;
        
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
            std::cout << "[DEBUG] TypeChecker::inferType - MemberAccess looking for type definition: " << typeName << std::endl;
            
            if (context) {
                TypeDefinition* typeDef = context->getTypeDefinition(typeName);
                if (typeDef) {
                    std::cout << "[DEBUG] TypeChecker::inferType - MemberAccess found type definition for: " << typeName << std::endl;
                    
                    // Get the fields from the type definition
                    const auto& fields = typeDef->getFields();
                    std::string memberName = memberAccess->getMember();
                    
                    std::cout << "[DEBUG] TypeChecker::inferType - MemberAccess searching for field: " << memberName << " in " << fields.size() << " fields" << std::endl;
                    
                    // Look for the field in the type definition
                    for (const auto& field : fields) {
                        std::string fieldName = field.first;
                        std::cout << "[DEBUG] TypeChecker::inferType - MemberAccess checking field: '" << fieldName << "'" << std::endl;
                        
                        // Extract the actual field name (remove type annotation if present)
                        size_t colonPos = fieldName.find(':');
                        if (colonPos != std::string::npos) {
                            fieldName = fieldName.substr(0, colonPos);
                            // Remove any trailing whitespace
                            fieldName.erase(fieldName.find_last_not_of(" \t") + 1);
                            std::cout << "[DEBUG] TypeChecker::inferType - MemberAccess extracted field name: '" << fieldName << "'" << std::endl;
                        }
                        
                        if (fieldName == memberName) {
                            std::cout << "[DEBUG] TypeChecker::inferType - MemberAccess found matching field: " << fieldName << std::endl;
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
                            
                            // Check if field has type annotation
                            std::string originalFieldName = field.first;
                            colonPos = originalFieldName.find(':');
                            if (colonPos != std::string::npos) {
                                std::string declaredType = originalFieldName.substr(colonPos + 1);
                                // Remove any leading/trailing whitespace
                                declaredType.erase(0, declaredType.find_first_not_of(" \t"));
                                declaredType.erase(declaredType.find_last_not_of(" \t") + 1);
                                
                                std::cout << "[DEBUG] TypeChecker::inferType - MemberAccess found type annotation: '" << declaredType << "'" << std::endl;
                                
                                if (declaredType == "String") {
                                    std::cout << "[DEBUG] TypeChecker::inferType - MemberAccess returning String type from annotation" << std::endl;
                                    return registry.getStringType();
                                } else if (declaredType == "Number") {
                                    std::cout << "[DEBUG] TypeChecker::inferType - MemberAccess returning Number type from annotation" << std::endl;
                                    return registry.getNumberType();
                                } else if (declaredType == "Boolean") {
                                    std::cout << "[DEBUG] TypeChecker::inferType - MemberAccess returning Boolean type from annotation" << std::endl;
                                    return registry.getBooleanType();
                                } else {
                                    // For custom types
                                    const Type* customType = registry.getType(declaredType);
                                    if (customType) {
                                        std::cout << "[DEBUG] TypeChecker::inferType - MemberAccess returning custom type from annotation: " << declaredType << std::endl;
                                        return customType;
                                    }
                                }
                            }
                            
                            // If no initialization expression and no type annotation, assume Number (common default)
                            return registry.getNumberType();
                        }
                    }
                }
            }
        }
        
        // Default fallback to Number type
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
        std::cout << "[DEBUG] TypeChecker::inferType - Processing IfExpression" << std::endl;
        
        // The type of an if expression is the type of its branches
        const Type* thenType = inferType(ifExpr->thenExpr, context);
        const Type* elseType = ifExpr->elseExpr ? inferType(ifExpr->elseExpr, context) : registry.getVoidType();
        
        std::cout << "[DEBUG] TypeChecker::inferType - IfExpression thenType: " << (thenType ? thenType->toString() : "null") << std::endl;
        std::cout << "[DEBUG] TypeChecker::inferType - IfExpression elseType: " << (elseType ? elseType->toString() : "null") << std::endl;
        
        if (thenType && elseType) {
            // If both types are exactly the same, return that type
            if (thenType->toString() == elseType->toString()) {
                std::cout << "[DEBUG] TypeChecker::inferType - IfExpression types are identical: " << thenType->toString() << std::endl;
                return thenType;
            }
            
            // Check if both types are user-defined types and find their common ancestor
            if (auto thenUserType = dynamic_cast<const UserDefinedType*>(thenType)) {
                if (auto elseUserType = dynamic_cast<const UserDefinedType*>(elseType)) {
                    // Both are user-defined types, check inheritance
                    std::string thenTypeName = thenUserType->getName();
                    std::string elseTypeName = elseUserType->getName();
                    
                    std::cout << "[DEBUG] TypeChecker::inferType - IfExpression checking common ancestor for: " << thenTypeName << " and " << elseTypeName << std::endl;
                    
                    // Check if they have a common ancestor
                    if (context) {
                        std::string commonAncestor = findCommonAncestor(thenTypeName, elseTypeName, context);
                        std::cout << "[DEBUG] TypeChecker::inferType - IfExpression common ancestor: " << commonAncestor << std::endl;
                        if (!commonAncestor.empty()) {
                            const Type* ancestorType = registry.getType(commonAncestor);
                            if (ancestorType) {
                                std::cout << "[DEBUG] TypeChecker::inferType - IfExpression returning common ancestor type: " << commonAncestor << std::endl;
                                return ancestorType;
                            } else {
                                std::cout << "[DEBUG] TypeChecker::inferType - IfExpression could not get ancestor type from registry, creating it" << std::endl;
                                // Try to create the type dynamically
                                const Type* dynamicType = registry.getType(commonAncestor);
                                if (dynamicType) {
                                    std::cout << "[DEBUG] TypeChecker::inferType - IfExpression successfully created dynamic ancestor type: " << commonAncestor << std::endl;
                                    return dynamicType;
                                }
                            }
                        }
                    }
                }
            }
            
            // Check if both types are compatible through inheritance
            if (areTypesCompatibleWithInheritance(thenType, elseType, context)) {
                std::cout << "[DEBUG] TypeChecker::inferType - IfExpression types are compatible through inheritance, returning: " << thenType->toString() << std::endl;
                return thenType;
            }
            
            // If no common type found, return the first type as fallback
            std::cout << "[DEBUG] TypeChecker::inferType - IfExpression no common ancestor found, returning first type: " << thenType->toString() << std::endl;
            return thenType;
        } else if (thenType) {
            std::cout << "[DEBUG] TypeChecker::inferType - IfExpression only then type found: " << thenType->toString() << std::endl;
            return thenType;
        } else if (elseType) {
            std::cout << "[DEBUG] TypeChecker::inferType - IfExpression only else type found: " << elseType->toString() << std::endl;
            return elseType;
        }
        
        std::cout << "[DEBUG] TypeChecker::inferType - IfExpression cannot determine type, returning null" << std::endl;
        return nullptr; // Type mismatch
    }
    
    // Let-in expressions
    if (auto letIn = dynamic_cast<LetIn*>(expr)) {
        // The type of a let-in expression is the type of its body
        Expression* inExpr = letIn->getInExpression();
        
        // If the in-expression is a block expression, we need to check if it actually returns a value
        if (auto blockExpr = dynamic_cast<BlockExpression*>(inExpr)) {
            const Type* resultType = inferType(blockExpr, context);
            // If the block only contains statements that don't return values (like for loops with only side effects),
            // the let-in expression should return void
            return resultType;
        }
        
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
                
                // For ForStatement, check if it produces a value
                if (auto forStmt = dynamic_cast<ForStatement*>(stmt)) {
                    // Get the last expression from the for loop body
                    BlockStatement* forBody = forStmt->getBody();
                    if (forBody) {
                        Expression* lastExpr = forBody->getLastExpression();
                        if (lastExpr) {
                            // Check if the last expression is just a print or assignment (side effects only)
                            if (dynamic_cast<Print*>(lastExpr) || dynamic_cast<AssignmentExpression*>(lastExpr)) {
                                // For loops with only side effects don't return a value
                                continue;
                            }
                            const Type* resultType = inferType(lastExpr, context);
                            return resultType;
                        }
                    }
                    // If no meaningful expression found in for body, this for loop doesn't return a value
                    continue;
                }
                
                // For WhileStatement, check if it produces a value
                if (auto whileStmt = dynamic_cast<WhileStatement*>(stmt)) {
                    // Get the last expression from the while loop body
                    BlockStatement* whileBody = whileStmt->getBody();
                    if (whileBody) {
                        Expression* lastExpr = whileBody->getLastExpression();
                        if (lastExpr) {
                            // Check if the last expression is just a print or assignment (side effects only)
                            if (dynamic_cast<Print*>(lastExpr) || dynamic_cast<AssignmentExpression*>(lastExpr)) {
                                // While loops with only side effects don't return a value
                                continue;
                            }
                            const Type* resultType = inferType(lastExpr, context);
                            return resultType;
                        }
                    }
                    // If no meaningful expression found in while body, this while loop doesn't return a value
                    continue;
                }
                
                // For Assignment statements, they don't contribute to return value (side effects only)
                if (dynamic_cast<Assignment*>(stmt)) {
                    continue;
                }
                
                // For IfStatement, we can't safely return it as an Expression
                // Instead, we'll return nullptr and let the caller handle this case
                if (dynamic_cast<IfStatement*>(stmt)) {
                    // IfStatements can't be treated as expressions in this context
                    // The type checker should handle function return type validation differently
                    continue;
                }
                
                // For ReturnStatement, we should handle this differently
                if (dynamic_cast<ReturnStatement*>(stmt)) {
                    continue;
                }

                // For other statement types that don't contribute to return value
                // we continue looking backwards
            }
        }

        // If we reach here, the block contains only statements that don't return values
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
    
    // While expressions
    if (auto whileExpr = dynamic_cast<WhileExpression*>(expr)) {
        std::cout << "[DEBUG] TypeChecker::inferType - Processing WhileExpression" << std::endl;
        
        // A while expression returns the type of its body's last expression
        BlockExpression* whileBody = whileExpr->getBody();
        if (whileBody) {
            std::cout << "[DEBUG] TypeChecker::inferType - WhileExpression has body, inferring body type" << std::endl;
            const Type* bodyType = inferType(whileBody, context);
            std::cout << "[DEBUG] TypeChecker::inferType - WhileExpression body type: " << (bodyType ? bodyType->toString() : "null") << std::endl;
            
            if (bodyType) {
                // Return the type of the body's last expression
                return bodyType;
            }
        }
        
        std::cout << "[DEBUG] TypeChecker::inferType - WhileExpression defaulting to Number" << std::endl;
        // If we can't determine the body type, default to Number (common case for loops with assignments)
        return registry.getNumberType();
    }
    
    // Default: unknown type
    return nullptr;
}

std::string TypeChecker::findCommonAncestor(const std::string& type1, const std::string& type2, IContext* context) {
    if (type1 == type2) {
        return type1;
    }
    
    if (!context) {
        return "";
    }
    
    // Get all ancestors of type1
    std::unordered_set<std::string> ancestors1;
    std::string current = type1;
    while (!current.empty() && current != "Object") {
        ancestors1.insert(current);
        current = context->getParentType(current);
    }
    ancestors1.insert("Object"); // Object is the root of all types
    
    // Walk up the hierarchy of type2 and find the first common ancestor
    current = type2;
    while (!current.empty()) {
        if (ancestors1.find(current) != ancestors1.end()) {
            return current;
        }
        if (current == "Object") {
            break;
        }
        current = context->getParentType(current);
    }
    
    // If no common ancestor found, return Object as the default
    return "Object";
}

const Type* TypeChecker::inferIfStatementType(IfStatement* ifStmt, IContext* context) {
    if (!ifStmt) return nullptr;
    
    std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Starting type inference for IfStatement" << std::endl;
    
    // Get the branches
    BlockStatement* thenBranch = ifStmt->getThenBranch();
    BlockStatement* elseBranch = ifStmt->getElseBranch();
    
    if (!thenBranch || !elseBranch) {
        std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Missing branch, returning void" << std::endl;
        // If there's no else branch, the if statement doesn't return a value
        return registry.getVoidType();
    }
    
    std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Both branches present" << std::endl;
    
    // Get the last expressions from both branches
    Expression* thenExpr = thenBranch->getLastExpression();
    Expression* elseExpr = elseBranch->getLastExpression();
    
    std::cout << "[DEBUG] TypeChecker::inferIfStatementType - thenExpr: " << (thenExpr ? "found" : "null") << std::endl;
    std::cout << "[DEBUG] TypeChecker::inferIfStatementType - elseExpr: " << (elseExpr ? "found" : "null") << std::endl;
    
    const Type* thenType = nullptr;
    const Type* elseType = nullptr;
    
    // Get type from then branch
    if (thenExpr) {
        thenType = inferType(thenExpr, context);
        std::cout << "[DEBUG] TypeChecker::inferIfStatementType - thenType from expression: " << (thenType ? thenType->toString() : "null") << std::endl;
    } else {
        // Check if the last statement is an IfStatement
        Statement* thenStmt = thenBranch->getLastStatement();
        if (auto thenIfStmt = dynamic_cast<IfStatement*>(thenStmt)) {
            thenType = inferIfStatementType(thenIfStmt, context);
            std::cout << "[DEBUG] TypeChecker::inferIfStatementType - thenType from nested if: " << (thenType ? thenType->toString() : "null") << std::endl;
        }
    }
    
    // Get type from else branch
    if (elseExpr) {
        elseType = inferType(elseExpr, context);
        std::cout << "[DEBUG] TypeChecker::inferIfStatementType - elseType from expression: " << (elseType ? elseType->toString() : "null") << std::endl;
    } else {
        // Check if the last statement is an IfStatement
        Statement* elseStmt = elseBranch->getLastStatement();
        if (auto elseIfStmt = dynamic_cast<IfStatement*>(elseStmt)) {
            elseType = inferIfStatementType(elseIfStmt, context);
            std::cout << "[DEBUG] TypeChecker::inferIfStatementType - elseType from nested if: " << (elseType ? elseType->toString() : "null") << std::endl;
        }
    }
    
    if (thenType && elseType) {
        std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Both types found: " << thenType->toString() << " and " << elseType->toString() << std::endl;
        
        // If both types are exactly the same, return that type
        if (thenType->toString() == elseType->toString()) {
            std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Types are identical, returning: " << thenType->toString() << std::endl;
            return thenType;
        }
        
        // Check if both types are user-defined types and find their common ancestor
        if (auto thenUserType = dynamic_cast<const UserDefinedType*>(thenType)) {
            if (auto elseUserType = dynamic_cast<const UserDefinedType*>(elseType)) {
                // Both are user-defined types, check inheritance
                std::string thenTypeName = thenUserType->getName();
                std::string elseTypeName = elseUserType->getName();
                
                std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Checking common ancestor for: " << thenTypeName << " and " << elseTypeName << std::endl;
                
                // Check if they have a common ancestor
                if (context) {
                    std::string commonAncestor = findCommonAncestor(thenTypeName, elseTypeName, context);
                    std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Common ancestor: " << commonAncestor << std::endl;
                    if (!commonAncestor.empty()) {
                        const Type* ancestorType = registry.getType(commonAncestor);
                        if (ancestorType) {
                            std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Returning common ancestor type: " << commonAncestor << std::endl;
                            return ancestorType;
                        } else {
                            std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Could not get ancestor type from registry, creating it" << std::endl;
                            // Try to create the type dynamically
                            const Type* dynamicType = registry.getType(commonAncestor);
                            if (dynamicType) {
                                std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Successfully created dynamic ancestor type: " << commonAncestor << std::endl;
                                return dynamicType;
                            }
                        }
                    }
                }
            }
        }
        
        // Check if both types are compatible through inheritance
        if (areTypesCompatibleWithInheritance(thenType, elseType, context)) {
            std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Types are compatible through inheritance, returning: " << thenType->toString() << std::endl;
            return thenType;
        }
        
        // If no common type found, return the first type as fallback
        std::cout << "[DEBUG] TypeChecker::inferIfStatementType - No common ancestor found, returning first type: " << thenType->toString() << std::endl;
        return thenType;
    } else if (thenType) {
        std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Only then type found: " << thenType->toString() << std::endl;
        return thenType;
    } else if (elseType) {
        std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Only else type found: " << elseType->toString() << std::endl;
        return elseType;
    }
    
    // Default to void if we can't determine the type
    std::cout << "[DEBUG] TypeChecker::inferIfStatementType - Cannot determine type, returning void" << std::endl;
    return registry.getVoidType();
}

bool TypeChecker::areTypesCompatible(const Type* expected, const Type* actual) const {
    if (!expected || !actual) return false;
    
    // Direct compatibility check
    return expected->isCompatibleWith(actual);
}

bool TypeChecker::areTypesCompatibleWithInheritance(const Type* expected, const Type* actual, IContext* context) const {
    if (!expected || !actual) return false;
    
    // Direct compatibility check
    if (expected->isCompatibleWith(actual)) {
        return true;
    }
    
    // Check inheritance compatibility for user-defined types
    if (auto expectedUser = dynamic_cast<const UserDefinedType*>(expected)) {
        if (auto actualUser = dynamic_cast<const UserDefinedType*>(actual)) {
            std::string expectedName = expectedUser->getName();
            std::string actualName = actualUser->getName();
            
            // Check if actual type is a subtype of expected type
            if (context && context->isSubtypeOf(actualName, expectedName)) {
                return true;
            }
        }
    }
    
    return false;
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

                        return registry.getType(returnTypeAnnotation);
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