#include "tree.hpp"
#include <iostream>

AsExpression::AsExpression(const SourceLocation& loc, Expression* object, const std::string& typeName)
    : Expression(loc), object(object), typeName(typeName) {}

AsExpression::~AsExpression() {
    delete object;
}

std::string AsExpression::toString() {
    return object->toString() + " as " + typeName;
}

void AsExpression::printNode(int depth) {
    printIndent(depth);
    std::cout << "AsExpression: " << typeName << std::endl;
    printIndent(depth + 1);
    std::cout << "Object:" << std::endl;
    object->printNode(depth + 2);
}

bool AsExpression::Validate(IContext* context) {
    // Validate the object expression
    if (!object->Validate(context)) {
        return false;
    }
    
    // Check if the type name exists
    if (!context->hasType(typeName)) {
        SEMANTIC_ERROR("Type '" + typeName + "' is not defined", location);
        return false;
    }
    
    return true;
}

llvm::Value* AsExpression::codegen(CodeGenerator& generator) {
    std::cout << "Generating code for as expression: " << typeName << std::endl;
    
    llvm::LLVMContext& context = generator.getContext();
    llvm::Module* module = generator.getModule();
    llvm::IRBuilder<>* builder = generator.getBuilder();
    
    // Generate code for the object expression
    llvm::Value* objectValue = object->codegen(generator);
    if (!objectValue) {
        return nullptr;
    }
    
    // Get the object's type
    llvm::Type* objectType = objectValue->getType();
    
    // If the object is an AllocaInst (local variable), we need to load its value
    llvm::Value* actualObjectValue = objectValue;
    if (llvm::isa<llvm::AllocaInst>(objectValue)) {
        llvm::AllocaInst* alloca = llvm::cast<llvm::AllocaInst>(objectValue);
        llvm::Type* allocatedType = alloca->getAllocatedType();
        actualObjectValue = builder->CreateLoad(allocatedType, alloca, "loaded_object");
        objectType = actualObjectValue->getType();
        std::cout << "Loaded value from AllocaInst, new type: ";
        objectType->print(llvm::errs());
        std::cout << std::endl;
    }
    
    // In this language, all types inherit from Object, including primitives
    // So we need to handle all types as potential objects for casting
    
    // Handle primitive types - they can be cast to Object or their own type
    if (objectType->isFloatTy()) {
        if (typeName == "Number" || typeName == "Object") {
            return actualObjectValue; // Already the correct type or can be treated as Object
        } else {
            // Cannot cast primitive Number to user-defined type
            std::cout << "Cannot cast from type: Number to " << typeName << std::endl;
            SEMANTIC_ERROR("Invalid cast: cannot cast from 'Number' to '" + typeName + "'", location);
            return nullptr;
        }
    } else if (objectType->isIntegerTy()) {
        if (typeName == "Boolean" || typeName == "Number" || typeName == "Object") {
            return actualObjectValue; // Already the correct type or can be treated as Object
        } else {
            // Cannot cast primitive Integer to user-defined type
            std::cout << "Cannot cast from type: Integer to " << typeName << std::endl;
            SEMANTIC_ERROR("Invalid cast: cannot cast from 'Integer' to '" + typeName + "'", location);
            return nullptr;
        }
    }
    
    // For pointer types, we need to be more careful
    // Objects created with 'new' should have runtime type information
    // Even if they appear as i8* at compile time, they may have runtime type info
    
    // Handle struct types (user-defined types)
    llvm::StructType* objectStructType = nullptr;
    if (objectType->isPointerTy()) {
        llvm::Type* pointedType = objectType->getPointerElementType();
        if (pointedType->isStructTy()) {
            objectStructType = llvm::cast<llvm::StructType>(pointedType);
            std::cout << "Found struct type for casting: " << objectStructType->getName().str() << std::endl;
        } else {
            // This might be a pointer to i8 that actually points to a struct with runtime type info
            // We'll proceed with runtime type checking
            std::cout << "Object appears to be a pointer to non-struct type, but may have runtime type info" << std::endl;
            std::cout << "Pointed type: ";
            pointedType->print(llvm::errs());
            std::cout << std::endl;
        }
    }
    
    // Special case: if this is a pointer to i8 but we're trying to cast to a user-defined type,
    // it might be a struct with runtime type information that appears as i8* due to bitcasting
    if (objectType->isPointerTy() && objectType->getPointerElementType()->isIntegerTy(8)) {
        // Check if we're trying to cast to a primitive type
        if (typeName == "String" || typeName == "Object") {
            return actualObjectValue; // Already the correct type or can be treated as Object
        }
        // For user-defined types, proceed with runtime type checking
        // Don't return an error here - let the runtime check determine if it's valid
        std::cout << "Object is i8* but target is user-defined type " << typeName << " - proceeding with runtime check" << std::endl;
    }
    
    // For downcasting, we need to get the target type from the context
    IContext* ctx = generator.getContextObject();
    if (!ctx || !ctx->hasType(typeName)) {
        SEMANTIC_ERROR("Target type '" + typeName + "' not found", location);
        return nullptr;
    }
    
    llvm::StructType* targetType = ctx->getType(typeName);
    if (!targetType) {
        SEMANTIC_ERROR("Target type '" + typeName + "' not found in LLVM context", location);
        return nullptr;
    }
    
    // For runtime casting, we need to create a runtime struct type that includes type information
    // The target should be a pointer to the runtime struct type (with type ID field)
    std::vector<llvm::Type*> runtimeTargetFields;
    runtimeTargetFields.push_back(llvm::Type::getInt8PtrTy(context)); // type ID field
    for (unsigned i = 0; i < targetType->getNumElements(); ++i) {
        runtimeTargetFields.push_back(targetType->getElementType(i));
    }
    llvm::StructType* runtimeTargetType = llvm::StructType::create(context, runtimeTargetFields, "runtime." + typeName);
    
    // Create or get the enhanced runtime type checking function
    llvm::Function* runtimeTypeCheckFunc = module->getFunction("__hulk_runtime_type_check_enhanced");
    if (!runtimeTypeCheckFunc) {
        // Create the enhanced runtime type checking function
        // int __hulk_runtime_type_check_enhanced(void* object, const char* typeName)
        llvm::FunctionType* funcType = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(context),
            {llvm::Type::getInt8PtrTy(context), llvm::Type::getInt8PtrTy(context)},
            false
        );
        runtimeTypeCheckFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            "__hulk_runtime_type_check_enhanced",
            module
        );
        
        // Implement the function body (same as in IsExpression)
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context, "entry", runtimeTypeCheckFunc);
        llvm::BasicBlock* checkBB = llvm::BasicBlock::Create(context, "check", runtimeTypeCheckFunc);
        llvm::BasicBlock* trueBB = llvm::BasicBlock::Create(context, "true", runtimeTypeCheckFunc);
        llvm::BasicBlock* falseBB = llvm::BasicBlock::Create(context, "false", runtimeTypeCheckFunc);
        
        llvm::IRBuilder<> funcBuilder(entryBB);
        
        // Get function arguments
        auto args = runtimeTypeCheckFunc->arg_begin();
        llvm::Value* objPtr = &*args++;
        llvm::Value* targetTypeName = &*args;
        
        // Cast object pointer to i8** to access the first field (type ID)
        llvm::Value* typeIdPtrPtr = funcBuilder.CreateBitCast(objPtr, llvm::Type::getInt8PtrTy(context)->getPointerTo(), "typeid.ptr.ptr");
        
        // Load the type ID string from the first field
        llvm::Value* actualTypeName = funcBuilder.CreateLoad(llvm::Type::getInt8PtrTy(context), typeIdPtrPtr, "actual.typename");
        
        // Check if actualTypeName is null (object without runtime type info)
        llvm::Value* isNull = funcBuilder.CreateICmpEQ(actualTypeName, llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(context)), "is.null");
        funcBuilder.CreateCondBr(isNull, falseBB, checkBB);
        
        // Check block: compare type names
        funcBuilder.SetInsertPoint(checkBB);
        
        // Create or get strcmp function
        llvm::Function* strcmpFunc = module->getFunction("strcmp");
        if (!strcmpFunc) {
            llvm::FunctionType* strcmpType = llvm::FunctionType::get(
                llvm::Type::getInt32Ty(context),
                {llvm::Type::getInt8PtrTy(context), llvm::Type::getInt8PtrTy(context)},
                false
            );
            strcmpFunc = llvm::Function::Create(
                strcmpType,
                llvm::Function::ExternalLinkage,
                "strcmp",
                module
            );
        }
        
        llvm::Value* cmpResult = funcBuilder.CreateCall(strcmpFunc, {actualTypeName, targetTypeName}, "strcmp.result");
        llvm::Value* isEqual = funcBuilder.CreateICmpEQ(cmpResult, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0), "is.equal");
        funcBuilder.CreateCondBr(isEqual, trueBB, falseBB);
        
        // True block
        funcBuilder.SetInsertPoint(trueBB);
        funcBuilder.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 1));
        
        // False block
        funcBuilder.SetInsertPoint(falseBB);
        funcBuilder.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0));
    }
    
    // Create or get the runtime error function for failed casts (declaration only)
    llvm::Function* runtimeErrorFunc = module->getFunction("__hulk_runtime_error");
    if (!runtimeErrorFunc) {
        llvm::FunctionType* errorFuncType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(context),
            {llvm::Type::getInt8PtrTy(context)},
            false
        );
        runtimeErrorFunc = llvm::Function::Create(
            errorFuncType,
            llvm::Function::ExternalLinkage,
            "__hulk_runtime_error",
            module
        );
        // Note: Function implementation is provided by the runtime library
    }
    
    // Cast object to void* and create type name string
    llvm::Value* voidPtr = builder->CreateBitCast(actualObjectValue, llvm::Type::getInt8PtrTy(context), "void.ptr");
    llvm::Constant* typeNameStr = builder->CreateGlobalStringPtr(typeName, "target.typename");
    
    // Call the enhanced runtime type checking function
    llvm::Value* typeCheckResult = builder->CreateCall(runtimeTypeCheckFunc, {voidPtr, typeNameStr}, "type.check.result");
    
    // Convert int result to bool (non-zero means true)
    llvm::Value* isValidCast = builder->CreateICmpNE(typeCheckResult, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0), "is.valid.cast");
    
    // Create basic blocks for the conditional cast
    llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* validCastBB = llvm::BasicBlock::Create(context, "valid_cast", currentFunc);
    llvm::BasicBlock* invalidCastBB = llvm::BasicBlock::Create(context, "invalid_cast", currentFunc);
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(context, "continue", currentFunc);
    
    // Branch based on the type check result
    builder->CreateCondBr(isValidCast, validCastBB, invalidCastBB);
    
    // Valid cast block: perform the cast
    builder->SetInsertPoint(validCastBB);
    // Cast to the runtime target type (which includes the type ID field)
    llvm::Value* castedValue = builder->CreateBitCast(actualObjectValue, runtimeTargetType->getPointerTo(), "casted.value");
    builder->CreateBr(continueBB);
    
    // Invalid cast block: call runtime error
    builder->SetInsertPoint(invalidCastBB);
    std::string errorMessage = "Runtime error: Cannot cast object to type '" + typeName + "'\\n";
    llvm::Constant* errorMsgStr = builder->CreateGlobalStringPtr(errorMessage, "cast.error.msg");
    builder->CreateCall(runtimeErrorFunc, {errorMsgStr});
    builder->CreateUnreachable();
    
    // Continue block: use the casted value
    builder->SetInsertPoint(continueBB);
    llvm::PHINode* result = builder->CreatePHI(runtimeTargetType->getPointerTo(), 1, "cast.result");
    result->addIncoming(castedValue, validCastBB);
    
    return result;
}