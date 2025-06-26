#include "tree.hpp"
#include <error_handler.hpp>
#include <iostream>

BaseExpression::BaseExpression(const SourceLocation& loc, const std::vector<Expression*>& args)
    : Expression(loc), args(args) {}

BaseExpression::~BaseExpression() {
    for (auto* arg : args) {
        delete arg;
    }
}

std::string BaseExpression::toString() {
    std::string result = "base(";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) result += ", ";
        result += args[i]->toString();
    }
    result += ")";
    return result;
}

void BaseExpression::printNode(int depth) {
    printIndent(depth);
    std::cout << "BaseExpression" << std::endl;
    for (auto* arg : args) {
        arg->printNode(depth + 1);
    }
}

bool BaseExpression::Validate(IContext* context) {
    // Validate all arguments
    for (auto* arg : args) {
        if (!arg->Validate(context)) {
            return false;
        }
    }
    
    // Check if we're inside a method that can call base()
    std::string currentType = context->getCurrentType();
    if (currentType.empty()) {
        SEMANTIC_ERROR("base() can only be called from within a method", location);
        return false;
    }
    
    // Check if the current type has a parent (other than Object)
    std::string parentType = context->getParentType(currentType);
    if (parentType.empty() || parentType == "Object") {
        SEMANTIC_ERROR("base() called in type '" + currentType + "' which has no parent type", location);
        return false;
    }
    
    return true;
}

llvm::Value* BaseExpression::codegen(CodeGenerator& generator) {
    std::cout << "Generating code for base() call" << std::endl;
    
    // Get the current method context
    std::string currentType = generator.getCurrentTypeName();
    std::string currentMethod = generator.getCurrentMethodName();
    
    if (currentType.empty() || currentMethod.empty()) {
        SEMANTIC_ERROR("base() can only be called from within a method", location);
        return nullptr;
    }
    
    // Get the current context to find parent type information
    IContext* context = generator.getContextObject();
    
    // Get the parent type
    std::string parentType = context->getParentType(currentType);
    if (parentType.empty() || parentType == "Object") {
        SEMANTIC_ERROR("base() called in type with no parent", location);
        return nullptr;
    }
    
    // Find the parent's version of the current method
    std::string parentMethodName = parentType + "." + currentMethod;
    llvm::Function* parentMethod = generator.getModule()->getFunction(parentMethodName);
    
    if (!parentMethod) {
        SEMANTIC_ERROR("Parent method '" + currentMethod + "' not found in type '" + parentType + "'", location);
        return nullptr;
    }
    
    // Get the 'self' value from the current scope
    llvm::Value* selfValue = generator.getNamedValue(std::string("self"));
    if (!selfValue) {
        SEMANTIC_ERROR("'self' not available in current scope", location);
        return nullptr;
    }
    
    // Cast self to the parent type if needed
    llvm::StructType* parentStructType = context->getType(parentType);
    if (parentStructType) {
        llvm::Type* parentPtrType = parentStructType->getPointerTo();
        if (selfValue->getType() != parentPtrType) {
            llvm::IRBuilder<>* builder = generator.getBuilder();
            selfValue = builder->CreateBitCast(selfValue, parentPtrType, "cast.to.parent");
        }
    }
    
    // Prepare arguments for the parent method call
    std::vector<llvm::Value*> argValues;
    argValues.push_back(selfValue); // 'self' is the first argument
    
    // Generate code for the arguments passed to base()
    for (auto* arg : args) {
        llvm::Value* argValue = arg->codegen(generator);
        if (!argValue) {
            return nullptr;
        }
        argValues.push_back(argValue);
    }
    
    // Call the parent method
    llvm::IRBuilder<>* builder = generator.getBuilder();
    return builder->CreateCall(parentMethod, argValues, "base.call." + currentMethod);
}