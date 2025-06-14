#include "tree.hpp"
#include "error_handler.hpp"
#include <iostream>

// Constructor implementations
UnaryOperation::UnaryOperation(Expression* operand, std::string operation)
    : Expression(), operand(operand), operation(operation) {}

UnaryOperation::UnaryOperation(const SourceLocation& loc, Expression* operand, std::string operation)
    : Expression(loc), operand(operand), operation(operation) {}

// Destructor
UnaryOperation::~UnaryOperation() {
    delete operand;
}

// toString implementation
std::string UnaryOperation::toString() {
    return "(" + operation + operand->toString() + ")";
}

// printNode implementation
void UnaryOperation::printNode(int depth) {
    printIndent(depth);
    std::cout << "UnaryOperation: " << operation << std::endl;
    operand->printNode(depth + 1);
}

// Validate implementation
bool UnaryOperation::Validate(IContext* context) {
    if (!operand->Validate(context)) {
        return false;
    }
    
    // For now, we only support unary minus on numeric expressions
    if (operation == "-") {
        // The operand should be a valid expression
        return true;
    }
    
    SEMANTIC_ERROR("Operador unario no soportado: " + operation, location);
    return false;
}

// Code generation implementation
llvm::Value* UnaryOperation::codegen(CodeGenerator& generator) {
    llvm::Value* operandValue = operand->codegen(generator);
    if (!operandValue) {
        return nullptr;
    }
    
    if (operation == "-") {
        // Check if the operand is a floating point or integer
        if (operandValue->getType()->isDoubleTy()) {
            return generator.getBuilder()->CreateFNeg(operandValue, "negtmp");
        } else if (operandValue->getType()->isIntegerTy()) {
            return generator.getBuilder()->CreateNeg(operandValue, "negtmp");
        } else if (operandValue->getType()->isFloatTy()) {
            return generator.getBuilder()->CreateFNeg(operandValue, "negtmp");
        } else {
            SEMANTIC_ERROR("Operador unario '-' no puede aplicarse a este tipo", location);
            return nullptr;
        }
    }
    
    SEMANTIC_ERROR("Operador unario no soportado: " + operation, location);
    return nullptr;
}