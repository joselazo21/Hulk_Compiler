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
    
    // If this type doesn't have its own constructor parameters, check if it inherits from a parent
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
            // No explicit parent arguments, so inherit constructor from parent
            std::string parentType = typeDef->getParentType();
            TypeDefinition* parentTypeDef = context->getTypeDefinition(parentType);
            if (parentTypeDef) {
                constructorParams = parentTypeDef->getParams();
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
    
    // Get field types from the type definition to validate argument types
    const auto& fields = typeDef->getFields();
    
    // Validate argument types against expected field types
    for (size_t i = 0; i < args.size() && i < fields.size(); ++i) {
        // Get the expected type from the field definition
        std::string fieldName = fields[i].first;
        std::string expectedType = "Number"; // Default
        
        // Extract type annotation from field name if present (e.g., "x:Number" -> "Number")
        size_t colonPos = fieldName.find(':');
        if (colonPos != std::string::npos) {
            expectedType = fieldName.substr(colonPos + 1);
            // Remove any leading/trailing whitespace
            expectedType.erase(0, expectedType.find_first_not_of(" \t"));
            expectedType.erase(expectedType.find_last_not_of(" \t") + 1);
        }
        
        // Infer the actual type of the argument
        std::string actualType = "Number"; // Default
        
        // Check if argument is a string literal
        if (auto* stringLit = dynamic_cast<StringLiteral*>(args[i])) {
            actualType = "String";
        }
        // Check if argument is a number literal
        else if (auto* numberLit = dynamic_cast<Number*>(args[i])) {
            actualType = "Number";
        }
        // For other expressions, we'd need more sophisticated type inference
        // For now, we'll assume they're valid
        
        // Check type compatibility
        if (expectedType != actualType) {
            SEMANTIC_ERROR("Constructor argument " + std::to_string(i + 1) + 
                          " for type '" + typeName + "' expects " + expectedType + 
                          " but got " + actualType, location);
            return false;
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
    // The new struct will have: [type_id_field, original_fields...]
    std::vector<llvm::Type*> newFieldTypes;
    
    // Add type ID field as the first field (i8* to store type name)
    newFieldTypes.push_back(llvm::Type::getInt8PtrTy(context));
    
    // Add all original fields
    for (unsigned i = 0; i < structType->getNumElements(); ++i) {
        newFieldTypes.push_back(structType->getElementType(i));
    }
    
    // Create the new struct type with runtime type information
    llvm::StructType* runtimeStructType = llvm::StructType::create(context, newFieldTypes, "runtime." + typeName);
    
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

    // Store the type name in the first field for runtime type checking
    llvm::Constant* typeNameStr = builder->CreateGlobalStringPtr(typeName, "typename." + typeName);
    llvm::Value* typeIdPtr = builder->CreateStructGEP(runtimeStructType, objectPtr, 0, "typeid.ptr");
    builder->CreateStore(typeNameStr, typeIdPtr);

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
        
        // Create an alloca for this parameter
        llvm::AllocaInst* alloca = builder->CreateAlloca(
            argValue->getType(), 
            nullptr, 
            constructorParams[i]
        );
        
        // Store the value
        builder->CreateStore(argValue, alloca);
        
        // Add to the symbol table
        generator.setNamedValue(constructorParams[i], alloca);
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
        if (i < static_cast<int>(evaluatedParentArgs.size())) {
            value = evaluatedParentArgs[i];
        } else if (i < static_cast<int>(args.size())) {
            // Fallback: use constructor argument if parentArgs not specified
            // Re-evaluate since we're in a new scope
            value = args[i]->codegen(generator);
        } else {
            // Default to appropriate zero value based on field type
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
        if (!value) {
            generator.popScope();
            return nullptr;
        }
        std::string fieldName = globalContext->getFieldName(typeName, i);
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
    
    std::cout << "[DEBUG] Assigning " << numOwnFields << " own fields for type " << typeName << std::endl;
    std::cout << "[DEBUG] Constructor params: ";
    for (size_t i = 0; i < constructorParams.size(); ++i) {
        std::cout << constructorParams[i] << " ";
    }
    std::cout << std::endl;
    
    for (int i = 0; i < numOwnFields; ++i) {
        llvm::Value* value = nullptr;
        int fieldIdx = ownFieldStartIdx + i;
        std::string fieldName = globalContext->getFieldName(typeName, fieldIdx);
        
        std::cout << "[DEBUG] Processing field " << i << " (fieldIdx=" << fieldIdx << ", fieldName=" << fieldName << ")" << std::endl;
        
        // First priority: try to use constructor parameter if available
        if (i < static_cast<int>(constructorParams.size())) {
            std::cout << "[DEBUG] Using constructor parameter " << constructorParams[i] << " for field " << fieldName << std::endl;
            // Load from the alloca we created earlier
            llvm::Value* alloca = generator.getNamedValue(constructorParams[i]);
            if (alloca && llvm::isa<llvm::AllocaInst>(alloca)) {
                llvm::AllocaInst* allocaInst = llvm::cast<llvm::AllocaInst>(alloca);
                value = builder->CreateLoad(allocaInst->getAllocatedType(), allocaInst, constructorParams[i]);
                std::cout << "[DEBUG] Successfully loaded constructor parameter value" << std::endl;
            } else {
                std::cout << "[DEBUG] Failed to find alloca for constructor parameter " << constructorParams[i] << std::endl;
            }
        }
        
        // Second priority: use field initialization expression if no constructor param
        if (!value && typeDef_local && i < static_cast<int>(typeDef_local->getFields().size())) {
            std::cout << "[DEBUG] Trying field initialization expression for field " << i << std::endl;
            Expression* fieldInitExpr = typeDef_local->getFields()[i].second;
            if (fieldInitExpr) {
                // Evaluate the field initialization expression
                value = fieldInitExpr->codegen(generator);
                std::cout << "[DEBUG] Field initialization expression evaluated" << std::endl;
            } else {
                std::cout << "[DEBUG] No field initialization expression found" << std::endl;
            }
        }
        
        // Last resort: default value
        if (!value) {
            std::cout << "[DEBUG] Using default value for field " << fieldName << std::endl;
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
        std::cout << "[DEBUG] Stored value for field " << fieldName << std::endl;
    }

    // Pop the temporary scope
    generator.popScope();

    return objectPtr;
}