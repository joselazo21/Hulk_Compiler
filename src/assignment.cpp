#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

Assignment::Assignment(const std::string& id, Expression* expr)
    : Statement(SourceLocation()), id(id), expr(expr) {}

Assignment::Assignment(const SourceLocation& loc, const std::string& id, Expression* expr)
    : Statement(loc), id(id), expr(expr) {}

void Assignment::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── Assignment: " << id << " = \n";
    if (expr) expr->printNode(depth + 1);
}

bool Assignment::Validate(IContext* context) {
    bool hasErrors = false;
    
    // Check if trying to assign to 'self' - this is not allowed
    if (id == "self") {
        SEMANTIC_ERROR("Cannot assign to 'self': 'self' is not a valid assignment target", location);
        hasErrors = true;
    }
    
    if (!expr->Validate(context)) {
        SEMANTIC_ERROR("Error in assignment to variable '" + id + "'", location);
        hasErrors = true;
    }
    
    // Check if variable exists and if types are compatible
    llvm::Type* varLLVMType = context->getVariableType(id);
    if (varLLVMType) {
        // Variable exists, check type compatibility
        llvm::Type* exprType = nullptr;
        
        // Infer the type of the expression being assigned
        if (dynamic_cast<Number*>(expr)) {
            exprType = llvm::Type::getInt32Ty(*context->getLLVMContext());
        } else if (dynamic_cast<StringLiteral*>(expr)) {
            exprType = llvm::Type::getInt8PtrTy(*context->getLLVMContext());
        } else {
            // For other expressions, assume Number type for now
            exprType = llvm::Type::getInt32Ty(*context->getLLVMContext());
        }
        
        // Check if types are compatible
        if (varLLVMType != exprType) {
            std::string expectedTypeName = "Unknown";
            std::string actualTypeName = "Unknown";
            
            if (varLLVMType->isIntegerTy(32)) {
                expectedTypeName = "Number";
            } else if (varLLVMType->isPointerTy() && 
                      varLLVMType->getPointerElementType()->isIntegerTy(8)) {
                expectedTypeName = "String";
            }
            
            if (exprType->isIntegerTy(32)) {
                actualTypeName = "Number";
            } else if (exprType->isPointerTy() && 
                      exprType->getPointerElementType()->isIntegerTy(8)) {
                actualTypeName = "String";
            }
            
            SEMANTIC_ERROR("Type mismatch in assignment to variable '" + id + 
                         "': expected " + expectedTypeName + ", got " + actualTypeName, location);
            hasErrors = true;
        }
    }
    
    return !hasErrors;
}

Assignment::~Assignment() {
    delete expr;
}

llvm::Value* Assignment::codegen(CodeGenerator& generator) {
    // 1. Generar valor de la expresión
    llvm::Value* val = expr->codegen(generator);
    if (!val) return nullptr;

    // 2. Buscar variable
    llvm::Value* variable = generator.getNamedValue(id);
    llvm::Type* varType = nullptr;
    llvm::LLVMContext& ctx = generator.getContext();

    // Detectar si es un iterador
    bool isIter = id.rfind("__iter_", 0) == 0;
    if (isIter) {
        // struct.range*
        llvm::StructType* rangeStruct = llvm::StructType::getTypeByName(ctx, "struct.range");
        if (!rangeStruct) {
            std::vector<llvm::Type*> members = {
                llvm::Type::getInt32Ty(ctx), // current
                llvm::Type::getInt32Ty(ctx)  // end
            };
            rangeStruct = llvm::StructType::create(ctx, members, "struct.range");
        }
        varType = rangeStruct->getPointerTo();
    } else {
        // For assignment (:=), the variable must already exist
        // We should use the type of the value being assigned
        varType = val->getType();
    }

    if (!variable) {
        // Assignment operator (:=) should only work on existing variables
        // If the variable doesn't exist, this is an error
        SEMANTIC_ERROR("Cannot assign to undefined variable '" + id + "'. Use 'let' to declare variables.", getLocation());
        return nullptr;
    }

    // 3. Almacenar valor (asegura que el tipo de val y variable coincidan)
    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(variable)) {
        if (val->getType() != alloca->getAllocatedType()) {
            SEMANTIC_ERROR("Type mismatch in assignment to variable '" + id + "'", getLocation());
            return nullptr;
        }
        generator.getBuilder()->CreateStore(val, alloca);
    } else {
        SEMANTIC_ERROR("Variable storage for '" + id + "' is not an alloca", getLocation());
        return nullptr;
    }
    return val;
}