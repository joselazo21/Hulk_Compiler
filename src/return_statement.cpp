#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

ReturnStatement::ReturnStatement(Expression* value)
    : Statement(SourceLocation()), value(value) {}

ReturnStatement::ReturnStatement(const SourceLocation& loc, Expression* value)
    : Statement(loc), value(value) {}

void ReturnStatement::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── Return Statement\n";
    if (value) {
        printIndent(depth);
        std::cout << "│   └── Value:\n";
        value->printNode(depth + 2);
    }
}

bool ReturnStatement::Validate(IContext* context) {
    if (value && !value->Validate(context)) {
        SEMANTIC_ERROR("Error in return value", location);
        return false;
    }
    return true;
}

ReturnStatement::~ReturnStatement() {
    if (value) delete value;
}

llvm::Value* ReturnStatement::codegen(CodeGenerator& generator) {
    llvm::Value* retVal = nullptr;
    
    // Get the current function to determine its return type
    llvm::Function* currentFunc = generator.getBuilder()->GetInsertBlock()->getParent();
    llvm::Type* expectedReturnType = nullptr;
    
    if (currentFunc) {
        expectedReturnType = currentFunc->getReturnType();
    }
    
    if (value) {
        // Generate the return value
        retVal = value->codegen(generator);
        if (!retVal) return nullptr;
        
        // Convert the return value to match the expected return type if necessary
        if (expectedReturnType && retVal->getType() != expectedReturnType) {
            retVal = convertValueToType(generator, retVal, expectedReturnType);
        }
    } else {
        // No return value specified, use default based on expected type
        if (expectedReturnType) {
            retVal = getDefaultValueForType(generator, expectedReturnType);
        } else {
            // Fallback to float 0.0 if we can't determine the type
            retVal = llvm::ConstantFP::get(llvm::Type::getFloatTy(generator.getContext()), 0.0);
        }
    }
    
    generator.getBuilder()->CreateRet(retVal);
    return retVal;
}

// Helper method to convert a value to the expected type
llvm::Value* ReturnStatement::convertValueToType(CodeGenerator& generator, llvm::Value* value, llvm::Type* targetType) {
    llvm::Type* sourceType = value->getType();
    
    // If types already match, no conversion needed
    if (sourceType == targetType) {
        return value;
    }
    
    llvm::IRBuilder<>* builder = generator.getBuilder();
    llvm::LLVMContext& context = generator.getContext();
    
    // Handle conversions between common types
    if (sourceType->isIntegerTy() && targetType->isFloatTy()) {
        // Integer to float conversion
        return builder->CreateSIToFP(value, targetType, "int_to_float");
    } else if (sourceType->isFloatTy() && targetType->isIntegerTy()) {
        // Float to integer conversion
        return builder->CreateFPToSI(value, targetType, "float_to_int");
    } else if (sourceType->isIntegerTy() && targetType->isIntegerTy(1)) {
        // Integer to boolean conversion
        return builder->CreateICmpNE(value, llvm::ConstantInt::get(sourceType, 0), "int_to_bool");
    } else if (sourceType->isFloatTy() && targetType->isIntegerTy(1)) {
        // Float to boolean conversion
        return builder->CreateFCmpONE(value, llvm::ConstantFP::get(sourceType, 0.0), "float_to_bool");
    } else if (sourceType->isIntegerTy(1) && targetType->isIntegerTy()) {
        // Boolean to integer conversion
        return builder->CreateZExt(value, targetType, "bool_to_int");
    } else if (sourceType->isIntegerTy(1) && targetType->isFloatTy()) {
        // Boolean to float conversion
        llvm::Value* intVal = builder->CreateZExt(value, llvm::Type::getInt32Ty(context), "bool_to_int");
        return builder->CreateSIToFP(intVal, targetType, "int_to_float");
    }
    
    // If no conversion is possible, return the original value
    // LLVM will handle or report the error
    return value;
}

// Helper method to get default value for a given type
llvm::Value* ReturnStatement::getDefaultValueForType(CodeGenerator& generator, llvm::Type* type) {
    llvm::LLVMContext& context = generator.getContext();
    
    if (type->isFloatTy()) {
        return llvm::ConstantFP::get(type, 0.0);
    } else if (type->isIntegerTy()) {
        return llvm::ConstantInt::get(type, 0);
    } else if (type->isPointerTy()) {
        return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type));
    }
    
    // Fallback to float 0.0
    return llvm::ConstantFP::get(llvm::Type::getFloatTy(context), 0.0);
}



