#include "tree.hpp"
#include <iostream>

IsExpression::IsExpression(const SourceLocation& loc, Expression* object, const std::string& typeName)
    : Expression(loc), object(object), typeName(typeName) {}

IsExpression::~IsExpression() {
    delete object;
}

std::string IsExpression::toString() {
    return object->toString() + " is " + typeName;
}

void IsExpression::printNode(int depth) {
    printIndent(depth);
    std::cout << "IsExpression: " << typeName << std::endl;
    printIndent(depth + 1);
    std::cout << "Object:" << std::endl;
    object->printNode(depth + 2);
}

bool IsExpression::Validate(IContext* context) {
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

llvm::Value* IsExpression::codegen(CodeGenerator& generator) {
    std::cout << "Generating code for is expression: " << typeName << std::endl;
    
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
    
    // Handle primitive types first
    if (objectType->isFloatTy()) {
        // For Number type
        llvm::Value* result = nullptr;
        if (typeName == "Number") {
            result = llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 1); // true
        } else {
            result = llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0); // false
        }
        return result;
    } else if (objectType->isIntegerTy(1)) {
        // For Boolean type
        llvm::Value* result = nullptr;
        if (typeName == "Boolean") {
            result = llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 1); // true
        } else {
            result = llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0); // false
        }
        return result;
    } else if (objectType->isIntegerTy(32)) {
        // For Integer type (also considered Number in some contexts)
        llvm::Value* result = nullptr;
        if (typeName == "Number" || typeName == "Integer") {
            result = llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 1); // true
        } else {
            result = llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0); // false
        }
        return result;
    } else if (objectType->isPointerTy() && objectType->getPointerElementType()->isIntegerTy(8)) {
        // For String type
        llvm::Value* result = nullptr;
        if (typeName == "String") {
            result = llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 1); // true
        } else {
            result = llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0); // false
        }
        return result;
    }
    
    // Handle object types (pointers to structs)
    if (!objectType->isPointerTy()) {
        // If it's not a pointer, it can't be an object type
        return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0); // false
    }
    
    // Check if it's a pointer to a struct (object type)
    llvm::Type* pointedType = objectType->getPointerElementType();
    if (!pointedType->isStructTy()) {
        // If it doesn't point to a struct, it's not an object type
        return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0); // false
    }
    
    llvm::StructType* objectStructType = llvm::cast<llvm::StructType>(pointedType);
    
    // For runtime type checking, we need to check if the object has runtime type information
    // We'll implement this by checking if the object has a type ID field as the first field
    
    // Create or get the runtime type checking function
    llvm::Function* runtimeTypeCheckFunc = module->getFunction("__hulk_runtime_type_check");
    if (!runtimeTypeCheckFunc) {
        // Create the runtime type checking function
        // bool __hulk_runtime_type_check(void* object, const char* typeName)
        llvm::FunctionType* funcType = llvm::FunctionType::get(
            llvm::Type::getInt1Ty(context),
            {llvm::Type::getInt8PtrTy(context), llvm::Type::getInt8PtrTy(context)},
            false
        );
        runtimeTypeCheckFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            "__hulk_runtime_type_check",
            module
        );
        
        // Implement the function body
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
    
    // Cast object to void* and create type name string
    llvm::Value* voidPtr = builder->CreateBitCast(objectValue, llvm::Type::getInt8PtrTy(context), "void.ptr");
    llvm::Constant* typeNameStr = builder->CreateGlobalStringPtr(typeName, "target.typename");
    
    // Call the runtime type checking function
    llvm::Value* result = builder->CreateCall(runtimeTypeCheckFunc, {voidPtr, typeNameStr}, "is.result");
    
    return result;
}