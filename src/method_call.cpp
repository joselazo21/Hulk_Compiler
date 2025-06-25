#include "tree.hpp"
#include <iostream>

// Helper function to check if a method exists in a type hierarchy
bool hasMethodInTypeHierarchy(IContext* context, const std::string& typeName, const std::string& methodName) {
    std::string currentType = typeName;
    
    // Walk up the inheritance chain
    while (currentType != "Object" && !currentType.empty()) {
        // Get the type definition
        TypeDefinition* typeDef = context->getTypeDefinition(currentType);
        if (typeDef) {
            // Check if the method exists in this type
            if (typeDef->getUseTypedMethods()) {
                const auto& methods = typeDef->getTypedMethods();
                for (const auto& method : methods) {
                    if (method.first == methodName) {
                        return true;
                    }
                }
            } else {
                const auto& methods = typeDef->getMethods();
                for (const auto& method : methods) {
                    // Extract method name from signature (e.g., "getX():Number" -> "getX")
                    std::string storedMethodName = method.first;
                    size_t parenPos = storedMethodName.find("(");
                    if (parenPos != std::string::npos) {
                        storedMethodName = storedMethodName.substr(0, parenPos);
                    }
                    if (storedMethodName == methodName) {
                        return true;
                    }
                }
            }
        }
        
        // Move to parent type
        std::string parentType = context->getParentType(currentType);
        if (parentType == currentType || parentType.empty()) {
            break;
        }
        currentType = parentType;
    }
    
    return false;
}

MethodCall::MethodCall(const SourceLocation& loc, Expression* object, const std::string& methodName, const std::vector<Expression*>& args)
    : Expression(loc), object(object), methodName(methodName), args(args) {}

MethodCall::~MethodCall() {
    delete object;
    for (auto* arg : args) {
        delete arg;
    }
}

std::string MethodCall::toString() {
    std::string result = object->toString() + "." + methodName + "(";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) result += ", ";
        result += args[i]->toString();
    }
    result += ")";
    return result;
}

void MethodCall::printNode(int depth) {
    printIndent(depth);
    std::cout << "MethodCall: " << methodName << std::endl;
    printIndent(depth + 1);
    std::cout << "Object:" << std::endl;
    object->printNode(depth + 2);
    printIndent(depth + 1);
    std::cout << "Arguments:" << std::endl;
    for (auto* arg : args) {
        arg->printNode(depth + 2);
    }
}

bool MethodCall::Validate(IContext* context) {
    // Validate the object expression
    if (!object->Validate(context)) {
        return false;
    }
    // Validate all arguments
    for (auto* arg : args) {
        if (!arg->Validate(context)) {
            return false;
        }
    }
    
    // Check if method exists in object's type
    // First, we need to determine the type of the object
    std::string objectTypeName;
    
    // If the object is a variable, get its semantic type name from context
    if (auto variable = dynamic_cast<Variable*>(object)) {
        objectTypeName = context->getVariableTypeName(variable->getName());
        std::cout << "[DEBUG] MethodCall::Validate - Variable '" << variable->getName() << "' has type '" << objectTypeName << "'" << std::endl;
        
        // If we don't have a semantic type name, fall back to LLVM type analysis
        if (objectTypeName.empty()) {
            std::cout << "[DEBUG] MethodCall::Validate - No semantic type found for '" << variable->getName() << "', trying LLVM type analysis" << std::endl;
            llvm::Type* varType = context->getVariableType(variable->getName());
            if (varType && varType->isPointerTy()) {
                llvm::Type* pointedType = varType->getPointerElementType();
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
    // If the object is a self expression, we need to get the current type context
    else if (dynamic_cast<SelfExpression*>(object)) {
        // Get the current type being processed
        objectTypeName = context->getCurrentType();
    }
    
    // If we have a type name, check if the method exists
    if (!objectTypeName.empty()) {
        // During validation phase, methods might not be registered in LLVM yet
        // So we need to check the type definitions directly
        if (!hasMethodInTypeHierarchy(context, objectTypeName, methodName)) {
            SEMANTIC_ERROR("Method '" + methodName + "' not found in type '" + objectTypeName + "'", location);
            return false;
        }
    }
    
    return true;
}

llvm::Value* MethodCall::codegen(CodeGenerator& generator) {
    std::cout << "Generating code for method call: " << methodName << std::endl;
    
    // Generate code for the object expression
    llvm::Value* objectValue = object->codegen(generator);
    if (!objectValue) {
        return nullptr;
    }
    
    // Get the builder once at the beginning
    llvm::IRBuilder<>* builder = generator.getBuilder();
    
    // First, try to determine the object type name from the AST
    std::string objectTypeName;
    
    // If the object is a variable, get its semantic type name from context
    if (auto variable = dynamic_cast<Variable*>(object)) {
        objectTypeName = generator.getContextObject()->getVariableTypeName(variable->getName());
        std::cout << "[DEBUG] MethodCall::codegen - Variable '" << variable->getName() << "' has semantic type '" << objectTypeName << "'" << std::endl;
        
        // If we don't have a semantic type name, try to debug the context chain
        if (objectTypeName.empty()) {
            std::cout << "[DEBUG] MethodCall::codegen - No semantic type found, checking context chain..." << std::endl;
            IContext* currentContext = generator.getContextObject();
            int depth = 0;
            while (currentContext && depth < 10) {
                std::string typeFromContext = currentContext->getVariableTypeName(variable->getName());
                std::cout << "[DEBUG] MethodCall::codegen - Context depth " << depth << ": type = '" << typeFromContext << "'" << std::endl;
                if (!typeFromContext.empty()) {
                    objectTypeName = typeFromContext;
                    break;
                }
                // Try to get parent context (this might not be directly accessible)
                depth++;
                break; // For now, just check the current context
            }
        }
    }
    // If the object is a function call, get the return type from the function
    else if (auto functionCall = dynamic_cast<FunctionCall*>(object)) {
        std::string functionName = functionCall->getFunctionName();
        objectTypeName = generator.getContextObject()->getFunctionReturnType(functionName);
        std::cout << "[DEBUG] MethodCall::codegen - Function call '" << functionName << "' returns type '" << objectTypeName << "'" << std::endl;
    }
    // If the object is a member access, we need to determine the type of the member
    else if (auto memberAccess = dynamic_cast<MemberAccess*>(object)) {
        // For member access, we need to look up the field type in the parent object
        std::cout << "[DEBUG] MethodCall::codegen - Member access detected for member: " << memberAccess->getMember() << std::endl;
        
        // Try to get the parent object type
        if (auto parentVar = dynamic_cast<Variable*>(memberAccess->getObject())) {
            std::string parentTypeName = generator.getContextObject()->getVariableTypeName(parentVar->getName());
            // Extract base type name if it has a numeric suffix
            if (!parentTypeName.empty()) {
                size_t dotPos = parentTypeName.find('.');
                if (dotPos != std::string::npos) {
                    parentTypeName = parentTypeName.substr(0, dotPos);
                }
            }
            if (!parentTypeName.empty()) {
                // Look up the field type in the parent type definition
                TypeDefinition* parentTypeDef = generator.getContextObject()->getTypeDefinition(parentTypeName);
                if (parentTypeDef) {
                    // Find the field and determine its type
                    const auto& fields = parentTypeDef->getFields();
                    for (const auto& field : fields) {
                        std::string fieldName = field.first;
                        // Remove type annotation if present
                        size_t colonPos = fieldName.find(':');
                        if (colonPos != std::string::npos) {
                            std::string fieldTypeName = fieldName.substr(colonPos + 1);
                            fieldName = fieldName.substr(0, colonPos);
                            // Remove whitespace
                            fieldName.erase(fieldName.find_last_not_of(" \t") + 1);
                            fieldTypeName.erase(0, fieldTypeName.find_first_not_of(" \t"));
                            fieldTypeName.erase(fieldTypeName.find_last_not_of(" \t") + 1);
                            
                            if (fieldName == memberAccess->getMember()) {
                                objectTypeName = fieldTypeName;
                                std::cout << "[DEBUG] MethodCall::codegen - Field '" << fieldName << "' has type '" << fieldTypeName << "'" << std::endl;
                                break;
                            }
                        }
                    }
                }
            }
        }
        else if (auto selfExpr = dynamic_cast<SelfExpression*>(memberAccess->getObject())) {
            // For self.member, get the current type and look up the member type
            std::string currentTypeName = generator.getContextObject()->getCurrentType();
            if (!currentTypeName.empty()) {
                TypeDefinition* typeDef = generator.getContextObject()->getTypeDefinition(currentTypeName);
                if (typeDef) {
                    const auto& fields = typeDef->getFields();
                    for (const auto& field : fields) {
                        std::string fieldName = field.first;
                        // Remove type annotation if present
                        size_t colonPos = fieldName.find(':');
                        if (colonPos != std::string::npos) {
                            std::string fieldTypeName = fieldName.substr(colonPos + 1);
                            fieldName = fieldName.substr(0, colonPos);
                            // Remove whitespace
                            fieldName.erase(fieldName.find_last_not_of(" \t") + 1);
                            fieldTypeName.erase(0, fieldTypeName.find_first_not_of(" \t"));
                            fieldTypeName.erase(fieldTypeName.find_last_not_of(" \t") + 1);
                            
                            if (fieldName == memberAccess->getMember()) {
                                objectTypeName = fieldTypeName;
                                std::cout << "[DEBUG] MethodCall::codegen - Self field '" << fieldName << "' has type '" << fieldTypeName << "'" << std::endl;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    // If the object is a self expression, get the current type
    else if (dynamic_cast<SelfExpression*>(object)) {
        objectTypeName = generator.getContextObject()->getCurrentType();
        std::cout << "[DEBUG] MethodCall::codegen - Self expression has type '" << objectTypeName << "'" << std::endl;
    }
    
    // If objectValue is a pointer to a field, we need to load it
    // But we need to be careful with runtime objects stored as i8*
    if (objectValue->getType()->isPointerTy()) {
        llvm::Type* pointedType = objectValue->getType()->getPointerElementType();
        if (!pointedType->isStructTy() && pointedType->isPointerTy()) {
            // This is likely a pointer to a pointer (i8** -> i8*), load it
            objectValue = builder->CreateLoad(pointedType, objectValue);
        }
    }
    
    // Get the object's type
    llvm::Type* objectType = objectValue->getType();
    
    // If it's a pointer, get the pointed-to type
    llvm::StructType* structType = nullptr;
    
    if (objectType->isPointerTy()) {
        llvm::Type* pointedType = objectType->getPointerElementType();
        if (pointedType->isStructTy()) {
            structType = llvm::cast<llvm::StructType>(pointedType);
        } else if (pointedType->isIntegerTy(8)) {
            // This is an i8* (generic pointer), we need to determine the actual type
            if (!objectTypeName.empty()) {
                // Extract the base type name (remove numeric suffix if present)
                std::string baseTypeName = objectTypeName;
                size_t dotPos = baseTypeName.find('.');
                if (dotPos != std::string::npos) {
                    baseTypeName = baseTypeName.substr(0, dotPos);
                }
                std::cout << "[DEBUG] MethodCall::codegen - Base type name: " << baseTypeName << std::endl;
                
                // Get the runtime struct type for this object
                std::string runtimeTypeName = "runtime." + baseTypeName;
                structType = generator.getContextObject()->getType(runtimeTypeName);
                if (structType) {
                    std::cout << "[DEBUG] MethodCall::codegen - Found runtime struct type: " << runtimeTypeName << std::endl;
                    // Cast the i8* back to the proper runtime struct type
                    llvm::Type* runtimePtrType = structType->getPointerTo();
                    objectValue = builder->CreateBitCast(objectValue, runtimePtrType, "cast.to.runtime");
                } else {
                    std::cout << "[DEBUG] MethodCall::codegen - Runtime struct not found, trying base struct: " << baseTypeName << std::endl;
                    // Try to get the base struct type instead
                    structType = generator.getContextObject()->getType(baseTypeName);
                    if (structType) {
                        std::cout << "[DEBUG] MethodCall::codegen - Found base struct type: " << baseTypeName << std::endl;
                        // Cast the i8* to the base struct type pointer
                        llvm::Type* basePtrType = structType->getPointerTo();
                        objectValue = builder->CreateBitCast(objectValue, basePtrType, "cast.to.base");
                    }
                }
            }
        }
    }
    
    if (!structType) {
        std::cout << "[ERROR] MethodCall::codegen - Cannot determine struct type for object" << std::endl;
        std::cout << "[ERROR] - Object type name: '" << objectTypeName << "'" << std::endl;
        std::cout << "[ERROR] - LLVM object type: ";
        objectType->print(llvm::errs());
        std::cout << std::endl;
        SEMANTIC_ERROR("Cannot call method on non-object type", location);
        return nullptr;
    }
    
    // Get the struct name from the type
    std::string structName;
    if (!objectTypeName.empty()) {
        // We already determined the object type name, extract base name
        structName = objectTypeName;
        size_t dotPos = structName.find('.');
        if (dotPos != std::string::npos) {
            structName = structName.substr(0, dotPos);
        }
    } else {
        structName = structType->getName().str();
        // Remove "struct." prefix if present
        if (structName.find("struct.") == 0) {
            structName = structName.substr(7);
        }
        // Remove "runtime." prefix if present (for runtime type checking structs)
        if (structName.find("runtime.") == 0) {
            structName = structName.substr(8);
        }
    }
    
    // Look up the method using the context (which handles inheritance)
    IContext* context = generator.getContextObject();
    llvm::Function* method = context->getMethod(structName, methodName);
    
    if (!method) {
        SEMANTIC_ERROR("Method '" + methodName + "' not found in type '" + structName + "'", location);
        return nullptr;
    }
    
    // Check if this method is overridden in any derived classes
    // If so, we need to use virtual dispatch
    bool needsVirtualDispatch = false;
    std::vector<std::string> derivedTypes;
    
    // Find all types that inherit from the current type and override this method
    llvm::Module* module = generator.getModule();
    for (auto& func : *module) {
        std::string funcName = func.getName().str();
        if (funcName.find("." + methodName) != std::string::npos) {
            std::string typeName = funcName.substr(0, funcName.find("." + methodName));
            // Check if this type is a subtype of our current type
            if (typeName != structName && context->isSubtypeOf(typeName, structName)) {
                derivedTypes.push_back(typeName);
                needsVirtualDispatch = true;
            }
        }
    }
    
    // Check if we need to cast the object to a parent type
    // Get the actual type that owns the method
    std::string methodOwnerType = structName;
    llvm::Function* currentMethod = nullptr;
    
    // Find which type actually owns this method by walking up the inheritance chain
    std::string currentType = structName;
    while (currentType != "Object") {
        currentMethod = generator.getModule()->getFunction(currentType + "." + methodName);
        if (currentMethod == method) {
            // Found the owner
            methodOwnerType = currentType;
            break;
        }
        currentType = context->getParentType(currentType);
        if (currentType.empty()) {
            currentType = "Object";
        }
    }
    
    // Generate code for the arguments
    std::vector<llvm::Value*> argValues;
    
    // Check if we need to handle runtime struct to original struct conversion
    std::string originalStructName = structType->getName().str();
    
    // If this is a runtime struct, we need to create a GEP to skip the type ID field
    if (originalStructName.find("runtime.") == 0) {
        std::string baseTypeName = originalStructName.substr(8); // Remove "runtime." prefix
        llvm::StructType* baseStructType = context->getType(baseTypeName);
        
        if (baseStructType) {
            // For runtime structs, we need to check if the method expects the base struct type
            // or if it can work with the runtime struct directly
            
            // First, check if the method exists for the runtime type
            llvm::Function* runtimeMethod = generator.getModule()->getFunction(originalStructName + "." + methodName);
            if (runtimeMethod) {
                // Method exists for runtime type, use it directly
                structName = originalStructName;
            } else {
                // Method doesn't exist for runtime type, need to convert to base type
                // Create a GEP to get a pointer to the data part (skipping TypeInfo at index 0)
                llvm::Value* indices[2] = {
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0),
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 1) // Skip TypeInfo field
                };
                
                // Get pointer to the first actual field (after TypeInfo)
                llvm::Value* dataPtr = builder->CreateInBoundsGEP(
                    structType, // Use the runtime struct type
                    objectValue,
                    {indices[0], indices[1]},
                    "data.ptr"
                );
                
                // Cast this pointer to the base struct type pointer
                llvm::Type* baseStructPtrType = baseStructType->getPointerTo();
                objectValue = builder->CreateBitCast(dataPtr, baseStructPtrType, "cast.to." + baseTypeName);
                structName = baseTypeName;
            }
        }
    }
    
    // If the method belongs to a parent type, cast the object pointer
    if (methodOwnerType != structName && methodOwnerType != "Object") {
        // Need to cast from child type to parent type
        llvm::StructType* parentStructType = context->getType(methodOwnerType);
        if (parentStructType) {
            llvm::Type* parentPtrType = parentStructType->getPointerTo();
            // Create a bitcast from the child type to the parent type
            objectValue = builder->CreateBitCast(objectValue, parentPtrType, "cast.to." + methodOwnerType);
            structName = methodOwnerType;
        }
    }
    
    // --- ADDITION: Always bitcast to the method's expected self type if needed ---
    // This ensures that even if the method is defined in a parent, the pointer type matches exactly.
    llvm::Type* expectedSelfType = nullptr;
    if (method) {
        if (method->arg_size() > 0) {
            expectedSelfType = method->getFunctionType()->getParamType(0);
        }
    }
    if (expectedSelfType && objectValue->getType() != expectedSelfType) {
        objectValue = builder->CreateBitCast(objectValue, expectedSelfType, "cast.to.method.self");
    }
    
    argValues.push_back(objectValue); // 'self' is the first argument
    
    for (size_t i = 0; i < args.size(); ++i) {
        llvm::Value* argValue = args[i]->codegen(generator);
        if (!argValue) {
            return nullptr;
        }
        
        // Check if we need to convert the argument type to match the expected parameter type
        if (method && i + 1 < method->getFunctionType()->getNumParams()) {
            llvm::Type* expectedType = method->getFunctionType()->getParamType(i + 1); // +1 because first param is 'self'
            llvm::Type* actualType = argValue->getType();
            
            // If types don't match, try to convert
            if (actualType != expectedType) {
                // Handle common type conversions
                if (expectedType->isFloatTy() && actualType->isIntegerTy()) {
                    // Convert integer to float
                    argValue = builder->CreateSIToFP(argValue, expectedType, "int_to_float");
                } else if (expectedType->isIntegerTy() && actualType->isFloatTy()) {
                    // Convert float to integer
                    argValue = builder->CreateFPToSI(argValue, expectedType, "float_to_int");
                } else if (expectedType->isFloatTy() && actualType->isFloatTy()) {
                    // Both are float types but different precision
                    if (actualType->isDoubleTy() && expectedType->isFloatTy()) {
                        argValue = builder->CreateFPTrunc(argValue, expectedType, "double_to_float");
                    } else if (actualType->isFloatTy() && expectedType->isDoubleTy()) {
                        argValue = builder->CreateFPExt(argValue, expectedType, "float_to_double");
                    }
                }
                // Add more type conversions as needed
            }
        }
        
        argValues.push_back(argValue);
    }
    
    // Debug: Print argument types for debugging
    for (size_t i = 0; i < argValues.size(); ++i) {
        std::string argTypeStr;
        llvm::raw_string_ostream rso(argTypeStr);
        argValues[i]->getType()->print(rso);
        std::cout << "[DEBUG] MethodCall::codegen - Argument " << i << " type: " << rso.str() << std::endl;
    }
    
    // Debug: Print expected parameter types
    if (method) {
        for (size_t i = 0; i < method->getFunctionType()->getNumParams(); ++i) {
            std::string paramTypeStr;
            llvm::raw_string_ostream rso(paramTypeStr);
            method->getFunctionType()->getParamType(i)->print(rso);
            std::cout << "[DEBUG] MethodCall::codegen - Expected parameter " << i << " type: " << rso.str() << std::endl;
        }
    }
    
    // If virtual dispatch is needed, generate runtime type checking
    llvm::Value* result = nullptr;
    
    if (needsVirtualDispatch && !derivedTypes.empty()) {
        std::cout << "[DEBUG] MethodCall::codegen - Using virtual dispatch for method '" << methodName << "'" << std::endl;
        
        // Get the current function and basic block
        llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock* currentBB = builder->GetInsertBlock();
        
        // Create the merge block where all paths will converge
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(generator.getContext(), "method.dispatch.merge", currentFunc);
        
        // Create PHI node for collecting results from different method calls
        llvm::PHINode* phi = llvm::PHINode::Create(
            method->getReturnType(),
            derivedTypes.size() + 1, // +1 for base case
            "method.result",
            mergeBB
        );
        
        // Get the runtime type checking function
        llvm::Function* typeCheckFunc = module->getFunction("__hulk_runtime_type_check_enhanced");
        if (!typeCheckFunc) {
            // Declare the runtime type checking function
            std::vector<llvm::Type*> typeCheckParams = {
                llvm::Type::getInt8PtrTy(generator.getContext()), // void* object
                llvm::Type::getInt8PtrTy(generator.getContext())  // const char* type_name
            };
            llvm::FunctionType* typeCheckFuncType = llvm::FunctionType::get(
                llvm::Type::getInt32Ty(generator.getContext()), // returns int
                typeCheckParams,
                false
            );
            typeCheckFunc = llvm::Function::Create(
                typeCheckFuncType,
                llvm::Function::ExternalLinkage,
                "__hulk_runtime_type_check_enhanced",
                module
            );
        }
        
        // Cast object to i8* for runtime type checking
        llvm::Value* objectAsI8Ptr = builder->CreateBitCast(objectValue, llvm::Type::getInt8PtrTy(generator.getContext()), "obj.as.i8ptr");
        
        llvm::BasicBlock* nextCheckBB = currentBB;
        
        // Generate checks for each derived type (in reverse order of inheritance depth)
        for (const std::string& derivedType : derivedTypes) {
            // Create basic blocks
            llvm::BasicBlock* checkBB = nextCheckBB;
            llvm::BasicBlock* callBB = llvm::BasicBlock::Create(generator.getContext(), "call." + derivedType, currentFunc);
            llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(generator.getContext(), "check.next." + derivedType, currentFunc);
            
            // Set insert point to the check block
            builder->SetInsertPoint(checkBB);
            
            // Create string constant for type name
            llvm::Value* typeNameStr = builder->CreateGlobalStringPtr(derivedType, "typename." + derivedType);
            
            // Call runtime type check
            llvm::Value* isType = builder->CreateCall(typeCheckFunc, {objectAsI8Ptr, typeNameStr}, "is." + derivedType);
            llvm::Value* isTypeCondition = builder->CreateICmpNE(isType, llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0), "is." + derivedType + ".cond");
            
            // Branch based on type check
            builder->CreateCondBr(isTypeCondition, callBB, nextBB);
            
            // Generate method call for this derived type
            builder->SetInsertPoint(callBB);
            
            // Get the method for this derived type
            llvm::Function* derivedMethod = module->getFunction(derivedType + "." + methodName);
            if (derivedMethod) {
                // Cast object to the derived type
                llvm::StructType* derivedStructType = context->getType(derivedType);
                if (derivedStructType) {
                    llvm::Value* derivedObjectValue = builder->CreateBitCast(objectValue, derivedStructType->getPointerTo(), "cast.to." + derivedType);
                    
                    // Prepare arguments for derived method call
                    std::vector<llvm::Value*> derivedArgValues;
                    derivedArgValues.push_back(derivedObjectValue);
                    for (size_t i = 1; i < argValues.size(); ++i) {
                        derivedArgValues.push_back(argValues[i]);
                    }
                    
                    // Call the derived method
                    llvm::Value* derivedResult = builder->CreateCall(derivedMethod, derivedArgValues, "call." + derivedType + "." + methodName);
                    
                    // Add to PHI node
                    phi->addIncoming(derivedResult, callBB);
                    
                    // Branch to merge block
                    builder->CreateBr(mergeBB);
                }
            }
            
            nextCheckBB = nextBB;
        }
        
        // Generate the default case (base method call)
        builder->SetInsertPoint(nextCheckBB);
        llvm::Value* baseResult = builder->CreateCall(method, argValues, "call.base." + methodName);
        phi->addIncoming(baseResult, nextCheckBB);
        builder->CreateBr(mergeBB);
        
        // Set insert point to merge block and return the PHI result
        builder->SetInsertPoint(mergeBB);
        result = phi;
        
    } else {
        // Use static dispatch (original behavior)
        result = builder->CreateCall(method, argValues, "call." + methodName);
    }

    if (result) {
        std::string resultTypeStr;
        llvm::raw_string_ostream rso(resultTypeStr);
        result->getType()->print(rso);
    }
    
    return result;
}