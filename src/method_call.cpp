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
        std::cout << "[DEBUG] MethodCall::Validate - Variable " << variable->getName() << " has type: " << objectTypeName << std::endl;
        
        // If we don't have a semantic type name, fall back to LLVM type analysis
        if (objectTypeName.empty()) {
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
    std::string objectTypeName;
    
    if (objectType->isPointerTy()) {
        llvm::Type* pointedType = objectType->getPointerElementType();
        if (pointedType->isStructTy()) {
            structType = llvm::cast<llvm::StructType>(pointedType);
        } else if (pointedType->isIntegerTy(8)) {
            // This is an i8* (generic pointer), we need to determine the actual type
            // Get the object type name from the variable context
            if (auto variable = dynamic_cast<Variable*>(object)) {
                objectTypeName = generator.getContextObject()->getVariableTypeName(variable->getName());
                std::cout << "[DEBUG] MethodCall::codegen - Variable " << variable->getName() << " has type name: " << objectTypeName << std::endl;
                if (!objectTypeName.empty()) {
                    // Get the runtime struct type for this object
                    std::string runtimeTypeName = "runtime." + objectTypeName;
                    std::cout << "[DEBUG] MethodCall::codegen - Looking for runtime type: " << runtimeTypeName << std::endl;
                    structType = generator.getContextObject()->getType(runtimeTypeName);
                    if (structType) {
                        std::cout << "[DEBUG] MethodCall::codegen - Found runtime struct type" << std::endl;
                        // Cast the i8* back to the proper runtime struct type
                        llvm::Type* runtimePtrType = structType->getPointerTo();
                        objectValue = builder->CreateBitCast(objectValue, runtimePtrType, "cast.to.runtime");
                    } else {
                        std::cout << "[DEBUG] MethodCall::codegen - Runtime struct type not found, trying base type" << std::endl;
                        // Try to get the base struct type instead
                        structType = generator.getContextObject()->getType(objectTypeName);
                        if (structType) {
                            std::cout << "[DEBUG] MethodCall::codegen - Found base struct type" << std::endl;
                            // Cast the i8* to the base struct type pointer
                            llvm::Type* basePtrType = structType->getPointerTo();
                            objectValue = builder->CreateBitCast(objectValue, basePtrType, "cast.to.base");
                        }
                    }
                } else {
                    std::cout << "[DEBUG] MethodCall::codegen - No type name found for variable" << std::endl;
                }
            }
        }
    }
    
    if (!structType) {
        SEMANTIC_ERROR("Cannot call method on non-object type", location);
        return nullptr;
    }
    
    // Get the struct name from the type
    std::string structName;
    if (!objectTypeName.empty()) {
        // We already determined the object type name
        structName = objectTypeName;
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
            std::cout << "[DEBUG] Converting runtime struct " << originalStructName << " to base struct " << baseTypeName << std::endl;
            
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
            
            std::cout << "[DEBUG] Created GEP to skip TypeInfo field" << std::endl;
            
            // Cast this pointer to the base struct type pointer
            llvm::Type* baseStructPtrType = baseStructType->getPointerTo();
            objectValue = builder->CreateBitCast(dataPtr, baseStructPtrType, "cast.to." + baseTypeName);
            structName = baseTypeName;
            
            std::cout << "[DEBUG] Cast to base struct type: " << baseTypeName << std::endl;
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
    
    for (auto* arg : args) {
        llvm::Value* argValue = arg->codegen(generator);
        if (!argValue) {
            return nullptr;
        }
        argValues.push_back(argValue);
    }
    
    // Debug: print method call information
    std::cout << "[DEBUG] Calling method " << methodName << " on object of type " << structName << std::endl;
    std::cout << "[DEBUG] Method function: " << (method ? method->getName().str() : "null") << std::endl;
    std::cout << "[DEBUG] Number of arguments: " << argValues.size() << std::endl;
    for (size_t i = 0; i < argValues.size(); ++i) {
        std::string argTypeStr;
        llvm::raw_string_ostream rso(argTypeStr);
        argValues[i]->getType()->print(rso);
        std::cout << "[DEBUG] Argument " << i << " type: " << argTypeStr << std::endl;
    }
    
    // Call the method
    llvm::Value* result = builder->CreateCall(method, argValues, "call." + methodName);
    
    // Debug: print result type
    if (result) {
        std::string resultTypeStr;
        llvm::raw_string_ostream rso(resultTypeStr);
        result->getType()->print(rso);
        std::cout << "[DEBUG] Method call result type: " << resultTypeStr << std::endl;
    }
    
    return result;
}