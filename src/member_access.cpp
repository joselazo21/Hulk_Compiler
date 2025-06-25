#include "tree.hpp"
#include <iostream>

MemberAccess::MemberAccess(const SourceLocation& loc, Expression* object, const std::string& member)
    : Expression(loc), object(object), member(member) {}

MemberAccess::~MemberAccess() {
    delete object;
}

std::string MemberAccess::toString() {
    return object->toString() + "." + member;
}

void MemberAccess::printNode(int depth) {
    printIndent(depth);
    std::cout << "MemberAccess: ." << member << std::endl;
    object->printNode(depth + 1);
}

bool MemberAccess::Validate(IContext* context) {
    // Validate the object expression
    if (!object->Validate(context)) {
        return false;
    }
    // TODO: Check if member exists in object's type
    return true;
}

llvm::Value* MemberAccess::codegen(CodeGenerator& generator) {
    std::cout << "Generating code for member access: " << member << std::endl;
    
    // Generate code for the object expression
    llvm::Value* objectValue = object->codegen(generator);
    if (!objectValue) {
        return nullptr;
    }
    
    // Get the object's type
    llvm::Type* objectType = objectValue->getType();
    
    // If it's a pointer, get the pointed-to type
    llvm::StructType* structType = nullptr;
    if (objectType->isPointerTy()) {
        llvm::Type* pointedType = objectType->getPointerElementType();
        if (pointedType->isStructTy()) {
            structType = llvm::cast<llvm::StructType>(pointedType);
        }
    }
    
    if (!structType) {
        SEMANTIC_ERROR("Cannot access member of non-object type", location);
        return nullptr;
    }
    
    // Get the struct name from the type
    std::string structName = structType->getName().str();
    std::string originalTypeName = structName;
    
    // Remove "struct." prefix if present
    if (structName.find("struct.") == 0) {
        structName = structName.substr(7);
    }
    // Remove "runtime." prefix if present (for runtime structs with type ID)
    if (structName.find("runtime.") == 0) {
        structName = structName.substr(8);
    }
    
    // Get the field index from the context
    IContext* context = generator.getContextObject();
    int fieldIndex = context->getFieldIndex(structName, member);
    
    if (fieldIndex < 0) {
        SEMANTIC_ERROR("Field '" + member + "' not found in type '" + structName + "'", location);
        return nullptr;
    }
    
    // Determine the actual field index based on struct type
    int actualFieldIndex;
    if (originalTypeName.find("runtime.") == 0) {
        // Runtime struct has type ID at index 0, so fields start at index 1
        actualFieldIndex = fieldIndex + 1;
    } else {
        // Regular struct, use field index as-is
        actualFieldIndex = fieldIndex;
    }
    
    // Create a GEP instruction to access the field
    llvm::IRBuilder<>* builder = generator.getBuilder();
    llvm::Value* indices[2] = {
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0),
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), actualFieldIndex)
    };
    
    // Create a pointer to the field using the actual struct type (not the original struct type)
    llvm::Value* fieldPtr = builder->CreateInBoundsGEP(
        structType,  // Use the actual struct type (runtime or regular)
        objectValue,
        {indices[0], indices[1]},
        "field." + member
    );
    
    llvm::Type* fieldType = fieldPtr->getType()->getPointerElementType();
    llvm::Value* fieldValue = builder->CreateLoad(fieldType, fieldPtr, "load." + member);
    
    return fieldValue;
}

llvm::Value* MemberAccess::getFieldPointer(CodeGenerator& generator) {
    std::cout << "Getting field pointer for member access: " << member << std::endl;
    
    // Generate code for the object expression
    llvm::Value* objectValue = object->codegen(generator);
    if (!objectValue) {
        return nullptr;
    }
    
    // Debug: Check if this is a self expression and what type it has
    if (auto selfExpr = dynamic_cast<SelfExpression*>(object)) {
        std::string selfType = generator.getContextObject()->getVariableTypeName("self");
        std::cout << "[DEBUG] MemberAccess::getFieldPointer - Self has type: '" << selfType << "'" << std::endl;
    }
    
    // Get the object's type
    llvm::Type* objectType = objectValue->getType();
    
    // If it's a pointer, get the pointed-to type
    llvm::StructType* structType = nullptr;
    if (objectType->isPointerTy()) {
        llvm::Type* pointedType = objectType->getPointerElementType();
        if (pointedType->isStructTy()) {
            structType = llvm::cast<llvm::StructType>(pointedType);
        }
    }
    
    if (!structType) {
        SEMANTIC_ERROR("Cannot access member of non-object type", location);
        return nullptr;
    }
    
    // Get the struct name from the type
    std::string structName = structType->getName().str();
    std::string originalTypeName = structName;
    
    // Remove "struct." prefix if present
    if (structName.find("struct.") == 0) {
        structName = structName.substr(7);
    }
    // Remove "runtime." prefix if present (for runtime structs with type ID)
    if (structName.find("runtime.") == 0) {
        structName = structName.substr(8);
    }
    
    // Get the field index from the context
    IContext* context = generator.getContextObject();
    int fieldIndex = context->getFieldIndex(structName, member);
    
    if (fieldIndex < 0) {
        SEMANTIC_ERROR("Field '" + member + "' not found in type '" + structName + "'", location);
        return nullptr;
    }
    
    // Determine the actual field index based on struct type
    int actualFieldIndex;
    if (originalTypeName.find("runtime.") == 0) {
        // Runtime struct has type ID at index 0, so fields start at index 1
        actualFieldIndex = fieldIndex + 1;
    } else {
        // Regular struct, use field index as-is
        actualFieldIndex = fieldIndex;
    }
    
    // Create a GEP instruction to access the field
    llvm::IRBuilder<>* builder = generator.getBuilder();
    llvm::Value* indices[2] = {
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0),
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), actualFieldIndex)
    };
    
    // Create a pointer to the field using the actual struct type (not the original struct type)
    llvm::Value* fieldPtr = builder->CreateInBoundsGEP(
        structType,  // Use the actual struct type (runtime or regular)
        objectValue,
        {indices[0], indices[1]},
        "field." + member
    );
    
    // Return the pointer (not the loaded value)
    return fieldPtr;
}