#include "tree.hpp"
#include <iostream>

NewExpression::NewExpression(const SourceLocation& loc, const std::string& typeName, const std::vector<Expression*>& args)
    : Expression(loc), typeName(typeName), args(args) {}

NewExpression::~NewExpression() {
    for (auto* arg : args) {
        delete arg;
    }
}

std::string NewExpression::toString() {
    std::string result = "new " + typeName + "(";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) result += ", ";
        result += args[i]->toString();
    }
    result += ")";
    return result;
}

void NewExpression::printNode(int depth) {
    printIndent(depth);
    std::cout << "NewExpression: " << typeName << std::endl;
    for (auto* arg : args) {
        arg->printNode(depth + 1);
    }
}

bool NewExpression::Validate(IContext* context) {
    // Check if type exists in context
    TypeDefinition* typeDef = context->getTypeDefinition(typeName);
    if (!typeDef) {
        SEMANTIC_ERROR("Type '" + typeName + "' not found", location);
        return false;
    }
    
    // Validate all arguments
    for (auto* arg : args) {
        if (!arg->Validate(context)) {
            return false;
        }
    }
    
    // Get constructor parameter types from the type definition
    std::vector<std::string> constructorParams = typeDef->getParams();
    
    // If this type doesn't have its own constructor parameters, traverse the inheritance chain
    if (constructorParams.empty() && typeDef->getParentType() != "Object") {
        // Check if parent has explicit parent arguments defined
        const auto& parentArgs = typeDef->getParentArgs();
        if (!parentArgs.empty()) {
            // If parent arguments are explicitly defined, no constructor arguments are expected
            if (args.size() != 0) {
                SEMANTIC_ERROR("Constructor for type '" + typeName + "' expects 0 arguments (parent arguments are explicitly defined), but got " + 
                              std::to_string(args.size()), location);
                return false;
            }
        } else {
            // No explicit parent arguments, so traverse inheritance chain to find constructor
            std::string currentType = typeDef->getParentType();
            while (currentType != "Object" && constructorParams.empty()) {
                TypeDefinition* currentTypeDef = context->getTypeDefinition(currentType);
                if (currentTypeDef) {
                    constructorParams = currentTypeDef->getParams();
                    if (constructorParams.empty()) {
                        // Continue up the inheritance chain
                        currentType = currentTypeDef->getParentType();
                    }
                } else {
                    // Type definition not found, break the loop
                    break;
                }
            }
        }
    }
    
    // Check if the number of arguments matches the number of constructor parameters
    if (args.size() != constructorParams.size()) {
        SEMANTIC_ERROR("Constructor for type '" + typeName + "' expects " + 
                      std::to_string(constructorParams.size()) + " arguments, but got " + 
                      std::to_string(args.size()), location);
        return false;
    }
    
    // Validate argument types against expected constructor parameter types
    // We need to find the type definition that actually defines the constructor parameters
    TypeDefinition* constructorOwnerTypeDef = nullptr;
    
    // Always traverse the inheritance chain to find where the constructor parameters are actually defined
    std::string currentType = typeName;
    while (currentType != "Object" && !constructorOwnerTypeDef) {
        TypeDefinition* currentTypeDef = context->getTypeDefinition(currentType);
        if (currentTypeDef) {
            std::vector<std::string> currentParams = currentTypeDef->getParams();
            if (!currentParams.empty()) {
                // Found the type that defines the constructor parameters
                constructorOwnerTypeDef = currentTypeDef;
                constructorParams = currentParams;
                break;
            }
            // Continue up the inheritance chain
            currentType = currentTypeDef->getParentType();
        } else {
            break;
        }
    }
    
    // If we still don't have constructor parameters, something is wrong
    if (!constructorOwnerTypeDef) {
        constructorOwnerTypeDef = typeDef; // Fallback to original type
    }
    



    for (const auto& param : constructorParams) {
        std::cout << param << " ";
    }
    std::cout << std::endl;
    
    // Now validate argument types against the constructor parameter types
    // We need to look at the constructor parameters and infer their expected types
    if (constructorOwnerTypeDef && !constructorParams.empty()) {
        const auto& constructorFields = constructorOwnerTypeDef->getFields();
        

        for (const auto& field : constructorFields) {
            std::cout << field.first << " ";
        }
        std::cout << std::endl;
        
        for (size_t i = 0; i < args.size() && i < constructorParams.size(); ++i) {
            // Extract parameter name and type annotation from constructor parameter
            std::string paramName = constructorParams[i];
            std::string expectedType = ""; // Will be inferred
            
            size_t paramColonPos = paramName.find(':');
            if (paramColonPos != std::string::npos) {
                expectedType = paramName.substr(paramColonPos + 1);
                paramName = paramName.substr(0, paramColonPos);
                // Remove any leading/trailing whitespace
                paramName.erase(paramName.find_last_not_of(" \t") + 1);
                expectedType.erase(0, expectedType.find_first_not_of(" \t"));
                expectedType.erase(expectedType.find_last_not_of(" \t") + 1);
            }
            
            // If no explicit type annotation, try to infer from field definitions
            if (expectedType.empty()) {
                // Look for a field that matches the constructor parameter name
                for (const auto& field : constructorFields) {
                    std::string fieldName = field.first;
                    std::string fieldType = "";
                    
                    // Extract the actual field name and type (remove type annotation if present)
                    size_t colonPos = fieldName.find(':');
                    if (colonPos != std::string::npos) {
                        fieldType = fieldName.substr(colonPos + 1);
                        fieldName = fieldName.substr(0, colonPos);
                        // Remove any leading/trailing whitespace
                        fieldName.erase(0, fieldName.find_first_not_of(" \t"));
                        fieldName.erase(fieldName.find_last_not_of(" \t") + 1);
                        fieldType.erase(0, fieldType.find_first_not_of(" \t"));
                        fieldType.erase(fieldType.find_last_not_of(" \t") + 1);
                    }
                    
                    // Check if this field corresponds to the constructor parameter
                    if (fieldName == paramName) {
                        expectedType = fieldType.empty() ? "Number" : fieldType; // Default to Number if no type annotation
                        break;
                    }
                }
                
                // If still no type found, default based on parameter name
                if (expectedType.empty()) {
                    expectedType = "Number"; // Default assumption
                }
            }
            
            // Infer the actual type of the argument
            std::string actualType = "";
            
            // Check if argument is a string literal
            if (dynamic_cast<StringLiteral*>(args[i])) {
                actualType = "String";
            }
            // Check if argument is a number literal
            else if (dynamic_cast<Number*>(args[i])) {
                actualType = "Number";
            }
            // Check if argument is a boolean literal
            else if (dynamic_cast<Boolean*>(args[i])) {
                actualType = "Boolean";
            }
            // Check if argument is a new expression (user-defined type)
            else if (auto* newExpr = dynamic_cast<NewExpression*>(args[i])) {
                actualType = newExpr->getTypeName();
            }
            // Check if argument is a variable - try to get its type from context
            else if (auto* var = dynamic_cast<Variable*>(args[i])) {
                std::string varTypeName = context->getVariableTypeName(var->getName());
                if (!varTypeName.empty()) {
                    actualType = varTypeName;
                } else {
                    // If no type information available, assume it's compatible
                    actualType = expectedType;
                }
            }
            // For other expressions, try to infer or assume compatibility
            else {
                // For complex expressions, we'll assume they're compatible for now
                // A more sophisticated type system would do full type inference here
                actualType = expectedType;
            }
            

            
            // Check type compatibility
            // Allow exact matches or inheritance relationships
            bool isCompatible = false;
            
            if (expectedType == actualType) {
                isCompatible = true;
            }
            // Check if actualType is a subtype of expectedType
            else if (context->hasType(actualType) && context->hasType(expectedType)) {
                isCompatible = context->isSubtypeOf(actualType, expectedType);
            }
            // Special case: allow Number literals where Number is expected
            else if (expectedType == "Number" && actualType == "Number") {
                isCompatible = true;
            }
            // Special case: allow String literals where String is expected
            else if (expectedType == "String" && actualType == "String") {
                isCompatible = true;
            }
            // Special case: allow Boolean literals where Boolean is expected
            else if (expectedType == "Boolean" && actualType == "Boolean") {
                isCompatible = true;
            }
            
            if (!isCompatible) {
                SEMANTIC_ERROR("Constructor argument " + std::to_string(i + 1) + 
                              " for type '" + typeName + "' expects " + expectedType + 
                              " but got " + actualType, location);
                return false;
            }
        }
    }
    
    return true;
}

llvm::Value* NewExpression::codegen(CodeGenerator& generator) {
    std::cout << "Generating code for new expression: " << typeName << std::endl;
    llvm::LLVMContext& context = generator.getContext();
    llvm::Module* module = generator.getModule();
    llvm::IRBuilder<>* builder = generator.getBuilder();

    // Use the global context object to look up types, not the current context from the stack
    IContext* globalContext = generator.getContextObject();
    if (!globalContext) {
        SEMANTIC_ERROR("Global context is null", location);
        return nullptr;
    }
    
    llvm::StructType* structType = globalContext->getType(typeName);

    if (!structType) {
        SEMANTIC_ERROR("Type '" + typeName + "' not found", location);
        return nullptr;
    }

    // Get the field count and names
    int totalFields = globalContext->getFieldCount(typeName);

    // Get parent type and parent argument expressions if any
    std::string parentType = globalContext->getParentType(typeName);
    int parentFieldCount = 0;
    std::vector<llvm::Value*> parentArgValues;
    if (parentType != "Object") {
        // Try to get parent argument expressions from the AST
        // We need to access the TypeDefinition node for this type to get parentArgs
        // For now, we assume the field order is: [parent fields][own fields]
        parentFieldCount = globalContext->getFieldCount(parentType);
    }

    // Create a new struct type that includes runtime type information
    // The new struct will have: [type_info_field, original_fields...]
    std::vector<llvm::Type*> newFieldTypes;
    
    // Create TypeInfo struct type if it doesn't exist
    llvm::StructType* typeInfoType = llvm::StructType::getTypeByName(context, "struct.TypeInfo");
    if (!typeInfoType) {
        std::vector<llvm::Type*> typeInfoFields;
        typeInfoFields.push_back(llvm::Type::getInt8PtrTy(context)); // type_name
        typeInfoFields.push_back(llvm::Type::getInt8PtrTy(context)); // parent_type_name (simplified)
        typeInfoType = llvm::StructType::create(context, typeInfoFields, "struct.TypeInfo");
    }
    
    // Add type info field as the first field (pointer to TypeInfo struct)
    newFieldTypes.push_back(typeInfoType->getPointerTo());
    
    // Add all original fields
    for (unsigned i = 0; i < structType->getNumElements(); ++i) {
        newFieldTypes.push_back(structType->getElementType(i));
    }
    
    // Create the new struct type with runtime type information
    llvm::StructType* runtimeStructType = llvm::StructType::create(context, newFieldTypes, "runtime." + typeName);
    
    // Register the runtime struct type in the context
    globalContext->addType("runtime." + typeName, runtimeStructType);

    
    // Allocate memory for the runtime struct (not the original struct)
    llvm::DataLayout dataLayout(module);
    uint64_t runtimeStructSize = dataLayout.getTypeAllocSize(runtimeStructType);

    llvm::Function* mallocFunc = module->getFunction("malloc");
    if (!mallocFunc) {
        llvm::FunctionType* mallocType = llvm::FunctionType::get(
            llvm::Type::getInt8PtrTy(context),
            {llvm::Type::getInt64Ty(context)},
            false
        );
        mallocFunc = llvm::Function::Create(
            mallocType,
            llvm::Function::ExternalLinkage,
            "malloc",
            module
        );
    }

    llvm::Value* sizeValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), runtimeStructSize);
    llvm::Value* mallocResult = builder->CreateCall(mallocFunc, {sizeValue}, "malloc.result");
    
    // Cast the malloc result to the runtime struct type
    llvm::Value* objectPtr = builder->CreateBitCast(mallocResult, runtimeStructType->getPointerTo(), "object.ptr");

    // Create and store TypeInfo structure for runtime type checking
    // First allocate memory for TypeInfo
    uint64_t typeInfoSize = dataLayout.getTypeAllocSize(typeInfoType);
    llvm::Value* typeInfoSizeValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), typeInfoSize);
    llvm::Value* typeInfoMalloc = builder->CreateCall(mallocFunc, {typeInfoSizeValue}, "typeinfo.malloc");
    llvm::Value* typeInfoPtr = builder->CreateBitCast(typeInfoMalloc, typeInfoType->getPointerTo(), "typeinfo.ptr");
    
    // Store type name in TypeInfo
    llvm::Constant* typeNameStr = builder->CreateGlobalStringPtr(typeName, "typename." + typeName);
    llvm::Value* typeNameFieldPtr = builder->CreateStructGEP(typeInfoType, typeInfoPtr, 0, "typename.field.ptr");
    builder->CreateStore(typeNameStr, typeNameFieldPtr);
    
    // Store parent type name in TypeInfo
    llvm::Constant* parentTypeNameStr = builder->CreateGlobalStringPtr(parentType, "parentname." + typeName);
    llvm::Value* parentNameFieldPtr = builder->CreateStructGEP(typeInfoType, typeInfoPtr, 1, "parentname.field.ptr");
    builder->CreateStore(parentTypeNameStr, parentNameFieldPtr);
    
    // Store TypeInfo pointer in the object's first field
    llvm::Value* objectTypeInfoPtr = builder->CreateStructGEP(runtimeStructType, objectPtr, 0, "object.typeinfo.ptr");
    builder->CreateStore(typeInfoPtr, objectTypeInfoPtr);

    // --- NEW LOGIC: handle parent arguments ---
    // We need to get the TypeDefinition AST node to access parentArgs.
    // We'll use a helper method on the context to get the parent argument expressions.
    // For now, we assume the context has a method getTypeDefinition(typeName) that returns the TypeDefinition*.
    // If not, you need to add this to your context.

    // Get the TypeDefinition AST node for this type
    TypeDefinition* typeDef = nullptr;
    if (globalContext) {
        typeDef = globalContext->getTypeDefinition(typeName);
    }
    std::vector<Expression*> parentArgs;
    if (typeDef && parentType != "Object") {
        parentArgs = typeDef->getParentArgs();
    }

    // First, we need to create a temporary scope with constructor parameters
    // This is necessary for evaluating parent arguments that may reference constructor params
    generator.pushScope();
    
    // Get constructor parameter names from the type definition
    std::vector<std::string> constructorParams;
    if (typeDef) {
        constructorParams = typeDef->getParams();
    }
    
    // Create allocas for constructor parameters and store their values
    for (size_t i = 0; i < constructorParams.size() && i < args.size(); ++i) {
        // Evaluate the argument
        llvm::Value* argValue = args[i]->codegen(generator);
        if (!argValue) {
            generator.popScope();
            return nullptr;
        }
        
        // Extract just the parameter name if it contains type annotation
        std::string paramName = constructorParams[i];
        size_t colonPos = paramName.find(':');
        if (colonPos != std::string::npos) {
            paramName = paramName.substr(0, colonPos);
            // Remove any trailing whitespace
            paramName.erase(paramName.find_last_not_of(" \t") + 1);
        }
        
        // Create an alloca for this parameter
        llvm::AllocaInst* alloca = builder->CreateAlloca(
            argValue->getType(), 
            nullptr, 
            paramName
        );
        
        // Store the value
        builder->CreateStore(argValue, alloca);
        
        // Add to the symbol table
        generator.setNamedValue(paramName, alloca);
    }

    // Now evaluate parent arguments with constructor parameters in scope
    std::vector<llvm::Value*> evaluatedParentArgs;
    for (auto* parentArg : parentArgs) {
        llvm::Value* value = parentArg->codegen(generator);
        if (!value) {
            generator.popScope();
            return nullptr;
        }
        evaluatedParentArgs.push_back(value);
    }

    // Assign parent fields using evaluated parent arguments

    for (int i = 0; i < parentFieldCount; ++i) {
        llvm::Value* value = nullptr;
        std::string fieldName = globalContext->getFieldName(typeName, i);

        
        if (i < static_cast<int>(evaluatedParentArgs.size())) {
            value = evaluatedParentArgs[i];

        } else if (i < static_cast<int>(args.size())) {
            // Fallback: use constructor argument if parentArgs not specified
            // Re-evaluate since we're in a new scope
            value = args[i]->codegen(generator);

        } else {
            // Try to get the field initialization expression by traversing the inheritance chain
            std::string currentType = parentType;
            bool foundField = false;
            
            while (currentType != "Object" && !foundField) {
                TypeDefinition* currentTypeDef = globalContext->getTypeDefinition(currentType);
                if (currentTypeDef) {
                    const auto& currentFields = currentTypeDef->getFields();
                    
                    // Find the field by name in the current type
                    for (const auto& currentField : currentFields) {
                        std::string currentFieldName = currentField.first;
                        
                        // Extract the actual field name (remove type annotation if present)
                        size_t colonPos = currentFieldName.find(':');
                        if (colonPos != std::string::npos) {
                            currentFieldName = currentFieldName.substr(0, colonPos);
                            // Remove any leading/trailing whitespace
                            currentFieldName.erase(0, currentFieldName.find_first_not_of(" \t"));
                            currentFieldName.erase(currentFieldName.find_last_not_of(" \t") + 1);
                        }
                        
                        if (currentFieldName == fieldName) {
                            Expression* fieldInitExpr = currentField.second;
                            if (fieldInitExpr) {
                                value = fieldInitExpr->codegen(generator);

                                foundField = true;
                                break;
                            }
                        }
                    }
                    
                    if (!foundField) {
                        // Move up the inheritance chain
                        currentType = currentTypeDef->getParentType();
                    }
                } else {
                    // Type definition not found, break the loop
                    break;
                }
            }
            
            if (!foundField) {

            }
            
            // If still no value, use default zero value
            if (!value) {
                llvm::Type* fieldType = runtimeStructType->getElementType(i + 1); // +1 for type ID field
                if (fieldType->isFloatTy()) {
                    value = llvm::ConstantFP::get(fieldType, 0.0);
                } else if (fieldType->isIntegerTy()) {
                    value = llvm::ConstantInt::get(fieldType, 0);
                } else if (fieldType->isPointerTy()) {
                    value = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(fieldType));
                } else {
                    value = llvm::Constant::getNullValue(fieldType);
                }

            }
        }
        if (!value) {

            generator.popScope();
            return nullptr;
        }
        

        std::string valueTypeStr;
        llvm::raw_string_ostream rso(valueTypeStr);
        value->getType()->print(rso);

        
        // Get the expected field type
        llvm::Type* expectedFieldType = runtimeStructType->getElementType(i + 1); // +1 for type ID field
        
        // Check if we need to cast the value to match the expected field type
        if (value->getType() != expectedFieldType) {
            // Handle pointer type mismatches (e.g., i8* vs specific struct pointer)
            if (value->getType()->isPointerTy() && expectedFieldType->isPointerTy()) {
                value = builder->CreateBitCast(value, expectedFieldType, "cast.field." + fieldName);
            }
            // Handle other type mismatches
            else if (value->getType()->isPointerTy() && expectedFieldType->isFloatTy()) {
                // This shouldn't happen in well-formed code, but handle gracefully
                std::cerr << "Warning: Type mismatch for field " << fieldName << ", using default value" << std::endl;
                value = llvm::ConstantFP::get(expectedFieldType, 0.0);
            }
        }
        
        llvm::Value* indices[2] = {
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), i + 1) // +1 to account for type ID field
        };
        llvm::Value* fieldPtr = builder->CreateInBoundsGEP(
            runtimeStructType, // Use runtime struct type instead of original struct type
            objectPtr,
            {indices[0], indices[1]},
            "field." + fieldName
        );
        builder->CreateStore(value, fieldPtr);

    }

    // Assign own fields (after parent fields)
    // Get the TypeDefinition to access field initialization expressions
    TypeDefinition* typeDef_local = globalContext->getTypeDefinition(typeName);
    
    int ownFieldStartIdx = parentFieldCount;
    int numOwnFields = totalFields - parentFieldCount;
    


    for (size_t i = 0; i < constructorParams.size(); ++i) {
        std::cout << constructorParams[i] << " ";
    }
    std::cout << std::endl;
    
    for (int i = 0; i < numOwnFields; ++i) {
        llvm::Value* value = nullptr;
        int fieldIdx = ownFieldStartIdx + i;
        std::string fieldName = globalContext->getFieldName(typeName, fieldIdx);
        

        
        // First priority: try to use constructor parameter if available
        if (i < static_cast<int>(constructorParams.size())) {
            // Extract just the parameter name if it contains type annotation
            std::string paramName = constructorParams[i];
            size_t colonPos = paramName.find(':');
            if (colonPos != std::string::npos) {
                paramName = paramName.substr(0, colonPos);
                // Remove any trailing whitespace
                paramName.erase(paramName.find_last_not_of(" \t") + 1);
            }
            

            // Load from the alloca we created earlier
            llvm::Value* alloca = generator.getNamedValue(paramName);
            if (alloca && llvm::isa<llvm::AllocaInst>(alloca)) {
                llvm::AllocaInst* allocaInst = llvm::cast<llvm::AllocaInst>(alloca);
                value = builder->CreateLoad(allocaInst->getAllocatedType(), allocaInst, paramName);

            } else {

            }
        }
        
        // Second priority: use field initialization expression if no constructor param
        if (!value && typeDef_local && i < static_cast<int>(typeDef_local->getFields().size())) {

            Expression* fieldInitExpr = typeDef_local->getFields()[i].second;
            if (fieldInitExpr) {
                // Evaluate the field initialization expression
                value = fieldInitExpr->codegen(generator);

            } else {

            }
        }
        
        // Last resort: default value
        if (!value) {

            // Default to appropriate zero value based on field type
            llvm::Type* fieldType = runtimeStructType->getElementType(fieldIdx + 1); // +1 for type ID field
            if (fieldType->isFloatTy()) {
                value = llvm::ConstantFP::get(fieldType, 0.0);
            } else if (fieldType->isIntegerTy()) {
                value = llvm::ConstantInt::get(fieldType, 0);
            } else if (fieldType->isPointerTy()) {
                value = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(fieldType));
            } else {
                value = llvm::Constant::getNullValue(fieldType);
            }
        }
        

        std::string valueTypeStr;
        llvm::raw_string_ostream rso1(valueTypeStr);
        value->getType()->print(rso1);
        
        std::string fieldTypeStr;
        llvm::raw_string_ostream rso2(fieldTypeStr);
        llvm::Type* expectedFieldType = runtimeStructType->getElementType(fieldIdx + 1);
        expectedFieldType->print(rso2);
        

        
        // Check if we need to cast the value to match the expected field type
        if (value->getType() != expectedFieldType) {
            // Handle pointer type mismatches (e.g., i8* vs specific struct pointer)
            if (value->getType()->isPointerTy() && expectedFieldType->isPointerTy()) {
                value = builder->CreateBitCast(value, expectedFieldType, "cast.field." + fieldName);
            }
            // Handle other type mismatches
            else if (value->getType()->isPointerTy() && expectedFieldType->isFloatTy()) {
                // This shouldn't happen in well-formed code, but handle gracefully
                std::cerr << "Warning: Type mismatch for field " << fieldName << ", using default value" << std::endl;
                value = llvm::ConstantFP::get(expectedFieldType, 0.0);
            }
        }
        
        llvm::Value* indices[2] = {
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0),
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), fieldIdx + 1) // +1 to account for type ID field
        };
        llvm::Value* fieldPtr = builder->CreateInBoundsGEP(
            runtimeStructType, // Use runtime struct type consistently
            objectPtr,
            {indices[0], indices[1]},
            "field." + fieldName
        );
        builder->CreateStore(value, fieldPtr);

    }

    // Pop the temporary scope
    generator.popScope();

    return objectPtr;
}