#include "tree.hpp"
#include "type_system.hpp"
#include <iostream>
#include <sstream>
#include "error_handler.hpp"

BinaryOperation::BinaryOperation(Expression* left, Expression* right, std::string operation)
    : Expression(), left(left), right(right), operation(operation) {}

BinaryOperation::BinaryOperation(const SourceLocation& loc, Expression* left, Expression* right, std::string operation)
    : Expression(loc), left(left), right(right), operation(operation) {}

BinaryOperation::~BinaryOperation() {
    delete left;
    delete right;
}

std::string BinaryOperation::toString() {
    std::stringstream ss;
    ss << "(" << left->toString() << " " << operation << " " << right->toString() << ")";
    return ss.str();
}

void BinaryOperation::printNode(int depth) {
    printIndent(depth);
    std::cout << "BinaryOperation: " << operation << std::endl;
    left->printNode(depth + 1);
    right->printNode(depth + 1);
}

bool BinaryOperation::Validate(IContext* context) {
    // First validate both operands - continue even if one fails
    bool leftValid = left->Validate(context);
    bool rightValid = right->Validate(context);
    
    // If either operand validation failed, return false but continue type checking
    bool hasErrors = !leftValid || !rightValid;
    
    // Create type registry and checker for strict type verification
    TypeRegistry typeRegistry;
    TypeChecker checker(typeRegistry);
    
    // Infer types of both operands
    const Type* leftType = checker.inferType(left, context);
    const Type* rightType = checker.inferType(right, context);
    
    if (!leftType) {
        SEMANTIC_ERROR("Cannot infer type for left operand in binary operation", location);
        hasErrors = true;
    }
    
    if (!rightType) {
        SEMANTIC_ERROR("Cannot infer type for right operand in binary operation", location);
        hasErrors = true;
    }
    
    // If we can't infer types, we can't continue with type checking
    if (!leftType || !rightType) {
        return !hasErrors;
    }
    
    std::cout << "[DEBUG] BinaryOperation::Validate - Left type: " << leftType->toString() 
              << ", Right type: " << rightType->toString() 
              << ", Operation: " << operation << std::endl;
    
    // Verify type compatibility based on operation type
    
    // Arithmetic operations: +, -, *, /, ^, %
    if (operation == "+" || operation == "-" || operation == "*" || 
        operation == "/" || operation == "^" || operation == "%") {
        
        // Both operands must be Number type for arithmetic operations
        auto leftBuiltin = dynamic_cast<const BuiltinTypeImpl*>(leftType);
        auto rightBuiltin = dynamic_cast<const BuiltinTypeImpl*>(rightType);
        
        if (!leftBuiltin || leftBuiltin->getBuiltinType() != BuiltinType::NUMBER) {
            SEMANTIC_ERROR("Left operand of arithmetic operation '" + operation + 
                         "' must be Number type, got " + leftType->toString(), location);
            hasErrors = true;
        }
        
        if (!rightBuiltin || rightBuiltin->getBuiltinType() != BuiltinType::NUMBER) {
            SEMANTIC_ERROR("Right operand of arithmetic operation '" + operation + 
                         "' must be Number type, got " + rightType->toString(), location);
            hasErrors = true;
        }
        
        if (!hasErrors) {
            std::cout << "[DEBUG] Arithmetic operation " << operation << " type check passed" << std::endl;
        }
        return !hasErrors;
    }
    
    // String concatenation operations: @, @@
    if (operation == "@" || operation == "@@") {
        // For string concatenation, we allow String + String, Number + String, String + Number
        auto leftBuiltin = dynamic_cast<const BuiltinTypeImpl*>(leftType);
        auto rightBuiltin = dynamic_cast<const BuiltinTypeImpl*>(rightType);
        
        bool leftValid = leftBuiltin && (leftBuiltin->getBuiltinType() == BuiltinType::STRING || 
                                        leftBuiltin->getBuiltinType() == BuiltinType::NUMBER);
        bool rightValid = rightBuiltin && (rightBuiltin->getBuiltinType() == BuiltinType::STRING || 
                                          rightBuiltin->getBuiltinType() == BuiltinType::NUMBER);
        
        if (!leftValid) {
            SEMANTIC_ERROR("Left operand of concatenation operation '" + operation + 
                         "' must be String or Number type, got " + leftType->toString(), location);
            hasErrors = true;
        }
        
        if (!rightValid) {
            SEMANTIC_ERROR("Right operand of concatenation operation '" + operation + 
                         "' must be String or Number type, got " + rightType->toString(), location);
            hasErrors = true;
        }
        
        if (!hasErrors) {
            std::cout << "[DEBUG] Concatenation operation " << operation << " type check passed" << std::endl;
        }
        return !hasErrors;
    }
    
    // Comparison operations: ==, !=, <, >, <=, >=
    if (operation == "==" || operation == "!=" || operation == "<" || 
        operation == ">" || operation == "<=" || operation == ">=") {
        
        // For comparison operations, both operands should be of compatible types
        if (!checker.areTypesCompatible(leftType, rightType)) {
            SEMANTIC_ERROR("Comparison operation '" + operation + 
                         "' requires compatible types, got " + leftType->toString() + 
                         " and " + rightType->toString(), location);
            hasErrors = true;
        }
        
        // For ordering comparisons (<, >, <=, >=), both operands must be Number
        if (operation != "==" && operation != "!=") {
            auto leftBuiltin = dynamic_cast<const BuiltinTypeImpl*>(leftType);
            auto rightBuiltin = dynamic_cast<const BuiltinTypeImpl*>(rightType);
            
            if (!leftBuiltin || leftBuiltin->getBuiltinType() != BuiltinType::NUMBER ||
                !rightBuiltin || rightBuiltin->getBuiltinType() != BuiltinType::NUMBER) {
                SEMANTIC_ERROR("Ordering comparison operation '" + operation + 
                             "' requires Number types, got " + leftType->toString() + 
                             " and " + rightType->toString(), location);
                hasErrors = true;
            }
        }
        
        if (!hasErrors) {
            std::cout << "[DEBUG] Comparison operation " << operation << " type check passed" << std::endl;
        }
        return !hasErrors;
    }
    
    // Logical operations: &&, ||
    if (operation == "&&" || operation == "||") {
        // For logical operations, operands should be Boolean-compatible
        // In this language, we consider Number 0 as false, non-zero as true
        // Boolean type is also acceptable
        auto leftBuiltin = dynamic_cast<const BuiltinTypeImpl*>(leftType);
        auto rightBuiltin = dynamic_cast<const BuiltinTypeImpl*>(rightType);
        
        bool leftValid = leftBuiltin && (leftBuiltin->getBuiltinType() == BuiltinType::BOOLEAN || 
                                        leftBuiltin->getBuiltinType() == BuiltinType::NUMBER);
        bool rightValid = rightBuiltin && (rightBuiltin->getBuiltinType() == BuiltinType::BOOLEAN || 
                                          rightBuiltin->getBuiltinType() == BuiltinType::NUMBER);
        
        if (!leftValid) {
            SEMANTIC_ERROR("Left operand of logical operation '" + operation + 
                         "' must be Boolean or Number type, got " + leftType->toString(), location);
            hasErrors = true;
        }
        
        if (!rightValid) {
            SEMANTIC_ERROR("Right operand of logical operation '" + operation + 
                         "' must be Boolean or Number type, got " + rightType->toString(), location);
            hasErrors = true;
        }
        
        if (!hasErrors) {
            std::cout << "[DEBUG] Logical operation " << operation << " type check passed" << std::endl;
        }
        return !hasErrors;
    }
    
    // If we reach here, it's an unknown operation
    SEMANTIC_ERROR("Unknown binary operation: " + operation, location);
    return false;
}

llvm::Value* BinaryOperation::codegen(CodeGenerator& generator) {
    llvm::Value* L = left->codegen(generator);
    llvm::Value* R = right->codegen(generator);
    
    if (!L || !R) return nullptr;
    
    auto& builder = *generator.getBuilder();
    
    // Handle string concatenation
    if (operation == "@" || operation == "@@") {
        // Convert operands to i8* if needed
        llvm::Value* leftStr = L;
        llvm::Value* rightStr = R;
        
        // Handle left operand
        if (L->getType()->isIntegerTy(32)) {
            leftStr = generator.intToString(L);
        } else if (L->getType()->isFloatTy()) {
            // Convert float to string
            leftStr = generator.floatToString(L);
        } else if (L->getType()->isPointerTy()) {
            llvm::Type* pointedType = L->getType()->getPointerElementType();
            if (pointedType->isIntegerTy(8)) {
                // Already a string pointer (i8*)
                leftStr = L;
            } else if (pointedType->isPointerTy() && pointedType->getPointerElementType()->isIntegerTy(8)) {
                // Pointer to string pointer (i8**) - load it
                leftStr = builder.CreateLoad(pointedType, L, "load_str_left");
            } else if (pointedType->isFloatTy()) {
                // Pointer to float - load it and convert to string
                llvm::Value* floatVal = builder.CreateLoad(pointedType, L, "load_float_left");
                leftStr = generator.floatToString(floatVal);
            } else if (pointedType->isIntegerTy(32)) {
                // Pointer to int - load it and convert to string
                llvm::Value* intVal = builder.CreateLoad(pointedType, L, "load_int_left");
                leftStr = generator.intToString(intVal);
            } else {
                SEMANTIC_ERROR("Left operand of @ must be string or number", getLocation());
                return nullptr;
            }
        } else {
            SEMANTIC_ERROR("Left operand of @ must be string or number", getLocation());
            return nullptr;
        }
        
        // Handle right operand
        if (R->getType()->isIntegerTy(32)) {
            rightStr = generator.intToString(R);
        } else if (R->getType()->isFloatTy()) {
            // Convert float to string
            rightStr = generator.floatToString(R);
        } else if (R->getType()->isPointerTy()) {
            llvm::Type* pointedType = R->getType()->getPointerElementType();
            if (pointedType->isIntegerTy(8)) {
                // Already a string pointer (i8*)
                rightStr = R;
            } else if (pointedType->isPointerTy() && pointedType->getPointerElementType()->isIntegerTy(8)) {
                // Pointer to string pointer (i8**) - load it
                rightStr = builder.CreateLoad(pointedType, R, "load_str_right");
            } else if (pointedType->isFloatTy()) {
                // Pointer to float - load it and convert to string
                llvm::Value* floatVal = builder.CreateLoad(pointedType, R, "load_float_right");
                rightStr = generator.floatToString(floatVal);
            } else if (pointedType->isIntegerTy(32)) {
                // Pointer to int - load it and convert to string
                llvm::Value* intVal = builder.CreateLoad(pointedType, R, "load_int_right");
                rightStr = generator.intToString(intVal);
            } else {
                SEMANTIC_ERROR("Right operand of @ must be string or number", getLocation());
                return nullptr;
            }
        } else {
            SEMANTIC_ERROR("Right operand of @ must be string or number", getLocation());
            return nullptr;
        }
        if (operation == "@@") {
            // For @@, we need to concatenate with a space in between
            llvm::Function* concatWithSpaceFunc = generator.getModule()->getFunction("string_concat_with_space");
            if (!concatWithSpaceFunc) {
                std::vector<llvm::Type*> argTypes = {
                    llvm::Type::getInt8PtrTy(generator.getContext()),
                    llvm::Type::getInt8PtrTy(generator.getContext())
                };
                llvm::FunctionType* funcType = llvm::FunctionType::get(
                    llvm::Type::getInt8PtrTy(generator.getContext()), argTypes, false);
                concatWithSpaceFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, 
                                                  "string_concat_with_space", generator.getModule());
            }
            return builder.CreateCall(concatWithSpaceFunc, {leftStr, rightStr}, "concat_ws_result");
        } else {
            // Regular @ concatenation
            llvm::Function* concatFunc = generator.getModule()->getFunction("string_concat");
            if (!concatFunc) {
                std::vector<llvm::Type*> argTypes = {
                    llvm::Type::getInt8PtrTy(generator.getContext()),
                    llvm::Type::getInt8PtrTy(generator.getContext())
                };
                llvm::FunctionType* funcType = llvm::FunctionType::get(
                    llvm::Type::getInt8PtrTy(generator.getContext()), argTypes, false);
                concatFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, 
                                                  "string_concat", generator.getModule());
            }
            return builder.CreateCall(concatFunc, {leftStr, rightStr}, "concat_result");
        }
    }
    
    // Convert both operands to float (f32) for arithmetic operations
    llvm::Type* floatType = llvm::Type::getFloatTy(generator.getContext());
    
    // Convert operands to float if needed
    if (L->getType()->isIntegerTy()) {
        L = builder.CreateSIToFP(L, floatType, "cast_l_to_float");
    } else if (L->getType()->isPointerTy()) {
        // If it's a pointer, load the value first
        llvm::Type* pointedType = L->getType()->getPointerElementType();
        if (pointedType->isFloatTy()) {
            L = builder.CreateLoad(pointedType, L, "load_l_float");
        } else if (pointedType->isIntegerTy()) {
            llvm::Value* intVal = builder.CreateLoad(pointedType, L, "load_l_int");
            L = builder.CreateSIToFP(intVal, floatType, "cast_l_to_float");
        } else if (pointedType->isDoubleTy()) {
            llvm::Value* doubleVal = builder.CreateLoad(pointedType, L, "load_l_double");
            L = builder.CreateFPTrunc(doubleVal, floatType, "trunc_l_to_float");
        }
    } else if (!L->getType()->isFloatTy()) {
        // If it's double, convert to float
        if (L->getType()->isDoubleTy()) {
            L = builder.CreateFPTrunc(L, floatType, "trunc_l_to_float");
        }
    }
    
    if (R->getType()->isIntegerTy()) {
        R = builder.CreateSIToFP(R, floatType, "cast_r_to_float");
    } else if (R->getType()->isPointerTy()) {
        // If it's a pointer, load the value first
        llvm::Type* pointedType = R->getType()->getPointerElementType();
        if (pointedType->isFloatTy()) {
            R = builder.CreateLoad(pointedType, R, "load_r_float");
        } else if (pointedType->isIntegerTy()) {
            llvm::Value* intVal = builder.CreateLoad(pointedType, R, "load_r_int");
            R = builder.CreateSIToFP(intVal, floatType, "cast_r_to_float");
        } else if (pointedType->isDoubleTy()) {
            llvm::Value* doubleVal = builder.CreateLoad(pointedType, R, "load_r_double");
            R = builder.CreateFPTrunc(doubleVal, floatType, "trunc_r_to_float");
        }
    } else if (!R->getType()->isFloatTy()) {
        // If it's double, convert to float
        if (R->getType()->isDoubleTy()) {
            R = builder.CreateFPTrunc(R, floatType, "trunc_r_to_float");
        }
    }
    
    // Arithmetic operations (all return float)
    if (operation == "+") return builder.CreateFAdd(L, R, "addtmp");
    if (operation == "-") return builder.CreateFSub(L, R, "subtmp");
    if (operation == "*") return builder.CreateFMul(L, R, "multmp");
    if (operation == "/") return builder.CreateFDiv(L, R, "divtmp");
    
    if (operation == "^") {
        // Power operation - convert to double for pow function, then back to float
        llvm::Value* Ld = builder.CreateFPExt(L, llvm::Type::getDoubleTy(generator.getContext()), "ext_l_to_double");
        llvm::Value* Rd = builder.CreateFPExt(R, llvm::Type::getDoubleTy(generator.getContext()), "ext_r_to_double");
        
        llvm::Function* powFunc = generator.getModule()->getFunction("pow");
        if (!powFunc) {
            std::vector<llvm::Type*> argTypes(2, llvm::Type::getDoubleTy(generator.getContext()));
            llvm::FunctionType* funcType = llvm::FunctionType::get(
                llvm::Type::getDoubleTy(generator.getContext()), argTypes, false);
            powFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "pow", generator.getModule());
        }
        llvm::Value* powResult = builder.CreateCall(powFunc, {Ld, Rd}, "powtmp");
        // Convert back to float
        return builder.CreateFPTrunc(powResult, floatType, "pow_float");
    }
    
    if (operation == "%") {
        // Modulo operation - convert to double for fmod function, then back to float
        llvm::Value* Ld = builder.CreateFPExt(L, llvm::Type::getDoubleTy(generator.getContext()), "ext_l_to_double");
        llvm::Value* Rd = builder.CreateFPExt(R, llvm::Type::getDoubleTy(generator.getContext()), "ext_r_to_double");
        
        llvm::Function* fmodFunc = generator.getModule()->getFunction("fmod");
        if (!fmodFunc) {
            std::vector<llvm::Type*> argTypes(2, llvm::Type::getDoubleTy(generator.getContext()));
            llvm::FunctionType* funcType = llvm::FunctionType::get(
                llvm::Type::getDoubleTy(generator.getContext()), argTypes, false);
            fmodFunc = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "fmod", generator.getModule());
        }
        llvm::Value* fmodResult = builder.CreateCall(fmodFunc, {Ld, Rd}, "modtmp");
        // Convert back to float
        return builder.CreateFPTrunc(fmodResult, floatType, "mod_float");
    }
    
    // Comparison operations (both operands are already float)
    if (operation == "==") {
        return builder.CreateFCmpOEQ(L, R, "eqtmp");
    }
    if (operation == "!=") {
        return builder.CreateFCmpONE(L, R, "netmp");
    }
    if (operation == "<") {
        return builder.CreateFCmpOLT(L, R, "lttmp");
    }
    if (operation == ">") {
        return builder.CreateFCmpOGT(L, R, "gttmp");
    }
    if (operation == "<=") {
        return builder.CreateFCmpOLE(L, R, "letmp");
    }
    if (operation == ">=") {
        return builder.CreateFCmpOGE(L, R, "getmp");
    }
    
    // Logical operations
    if (operation == "&&" || operation == "||") {
        llvm::Value* Lbool;
        llvm::Value* Rbool;
        
        // Convert operands to boolean values (i1)
        if (L->getType()->isIntegerTy(1)) {
            // Already a boolean
            Lbool = L;
        } else if (L->getType()->isFloatTy()) {
            // Compare float with 0.0f
            Lbool = builder.CreateFCmpONE(L, llvm::ConstantFP::get(llvm::Type::getFloatTy(generator.getContext()), 0.0f), "Lbool");
        } else if (L->getType()->isIntegerTy()) {
            // Ensure we use the correct type for the constant
            llvm::Type* LType = L->getType();
            Lbool = builder.CreateICmpNE(L, llvm::ConstantInt::get(LType, 0), "Lbool");
        } else {
            Lbool = builder.CreateFCmpONE(L, llvm::ConstantFP::get(generator.getContext(), llvm::APFloat(0.0)), "Lbool");
        }
        
        if (R->getType()->isIntegerTy(1)) {
            // Already a boolean
            Rbool = R;
        } else if (R->getType()->isFloatTy()) {
            // Compare float with 0.0f
            Rbool = builder.CreateFCmpONE(R, llvm::ConstantFP::get(llvm::Type::getFloatTy(generator.getContext()), 0.0f), "Rbool");
        } else if (R->getType()->isIntegerTy()) {
            // Ensure we use the correct type for the constant
            llvm::Type* RType = R->getType();
            Rbool = builder.CreateICmpNE(R, llvm::ConstantInt::get(RType, 0), "Rbool");
        } else {
            Rbool = builder.CreateFCmpONE(R, llvm::ConstantFP::get(generator.getContext(), llvm::APFloat(0.0)), "Rbool");
        }
        
        // Return i1 (boolean) type directly
        if (operation == "&&") {
            return builder.CreateAnd(Lbool, Rbool, "andtmp");
        } else {
            return builder.CreateOr(Lbool, Rbool, "ortmp");
        }
    }
    
    CODEGEN_ERROR("Unknown binary operator: " + operation, location);
    return nullptr;
}