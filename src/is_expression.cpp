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
        std::cout << "[DEBUG] IsExpression::codegen - objectValue is null!" << std::endl;
        return nullptr;
    }
    
    // Get the object's type
    llvm::Type* objectType = objectValue->getType();
    std::cout << "[DEBUG] IsExpression::codegen - objectType: " << objectType << std::endl;
    
    // Check if this is a literal string (global string constant)
    bool isLiteralString = false;
    if (llvm::isa<llvm::GlobalVariable>(objectValue)) {
        llvm::GlobalVariable* gv = llvm::cast<llvm::GlobalVariable>(objectValue);
        if (gv->hasInitializer() && llvm::isa<llvm::ConstantDataArray>(gv->getInitializer())) {
            isLiteralString = true;
        }
    } else if (llvm::isa<llvm::ConstantExpr>(objectValue)) {
        llvm::ConstantExpr* ce = llvm::cast<llvm::ConstantExpr>(objectValue);
        if (ce->getOpcode() == llvm::Instruction::GetElementPtr) {
            isLiteralString = true;
        }
    }
    
    // Handle primitive types first - but only for actual primitives, not object pointers
    if (objectType->isFloatTy()) {
        // For Number type - check if target type is Number or Object
        if (typeName == "Number" || typeName == "Object") {
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 1); // true
        } else {
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0); // false
        }
    } else if (objectType->isIntegerTy(1)) {
        // For Boolean type - check if target type is Boolean or Object
        if (typeName == "Boolean" || typeName == "Object") {
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 1); // true
        } else {
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0); // false
        }
    } else if (objectType->isIntegerTy(32)) {
        std::cout << "[DEBUG] IsExpression::codegen - Integer type detected" << std::endl;
        // For Integer type - check if target type is Number, Integer, or Object
        if (typeName == "Number" || typeName == "Integer" || typeName == "Object") {
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 1); // true
        } else {
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0); // false
        }
    } else if (isLiteralString) {
        std::cout << "[DEBUG] IsExpression::codegen - Literal string type detected" << std::endl;
        // For String literals - check if target type is String or Object
        if (typeName == "String" || typeName == "Object") {
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 1); // true
        } else {
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0); // false
        }
    }
    
    // Create or get the enhanced runtime type checking function
    llvm::Function* runtimeTypeCheckFunc = module->getFunction("__hulk_runtime_type_check_enhanced");
    if (!runtimeTypeCheckFunc) {
        // Create the enhanced runtime type checking function
        // int __hulk_runtime_type_check_enhanced(void* object, const char* typeName)
        llvm::FunctionType* funcType = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(context), // Return int instead of bool for compatibility
            {llvm::Type::getInt8PtrTy(context), llvm::Type::getInt8PtrTy(context)},
            false
        );
        runtimeTypeCheckFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            "__hulk_runtime_type_check_enhanced",
            module
        );
    } else {
        std::cout << "[DEBUG] IsExpression::codegen - Runtime type check function already exists" << std::endl;
    }
    
    // Cast object to void* and create type name string
    llvm::Value* voidPtr = nullptr;
    if (objectType->isPointerTy()) {
        // Already a pointer, just cast to i8*
        voidPtr = builder->CreateBitCast(objectValue, llvm::Type::getInt8PtrTy(context), "void.ptr");
    } else {
        // Not a pointer, this shouldn't happen for objects but handle it gracefully
        return llvm::ConstantInt::get(llvm::Type::getInt1Ty(context), 0); // false
    }
    
    llvm::Constant* typeNameStr = builder->CreateGlobalStringPtr(typeName, "target.typename");
    
    // Call the enhanced runtime type checking function
    llvm::Value* result = builder->CreateCall(runtimeTypeCheckFunc, {voidPtr, typeNameStr}, "is.result");
    
    // Convert int result to bool (non-zero means true)
    llvm::Value* boolResult = builder->CreateICmpNE(result, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0), "is.bool.result");
    
    return boolResult;
}