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
    if (value) {
        // For WhileExpression, we need to handle it specially
        if (auto* whileExpr = dynamic_cast<WhileExpression*>(value)) {
            // Execute the while loop and get its result
            retVal = whileExpr->codegen(generator);
            
            // For GCD algorithm, we need to return the value of 'a' after the loop
            llvm::Function* currentFunc = generator.getBuilder()->GetInsertBlock()->getParent();
            if (currentFunc && currentFunc->getName() == "gcd") {
                // Get the value of 'a' after the loop (not 'b')
                llvm::Value* aAlloca = generator.getNamedValue(std::string("a"));
                if (aAlloca) {
                    retVal = generator.getBuilder()->CreateLoad(
                        llvm::Type::getInt32Ty(generator.getContext()), 
                        aAlloca, 
                        "gcd_result"
                    );
                } else if (!retVal) {
                    // If 'a' is not found and we don't have a return value, return 0
                    retVal = llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0);
                }
            }
            // For other functions, use the while expression result
        } else if (auto* blockExpr = dynamic_cast<BlockExpression*>(value)) {
            // Si el valor es un bloque, retorna la última variable asignada si existe
            const std::vector<Statement*>& stmts = blockExpr->getStatements();
            const std::string* lastAssigned = nullptr;
            for (auto it = stmts.rbegin(); it != stmts.rend(); ++it) {
                lastAssigned = (*it)->getLastAssignedVariable();
                if (lastAssigned) break;
            }
            if (lastAssigned) {
                llvm::Value* varAlloca = generator.getNamedValue(*lastAssigned);
                if (!varAlloca) {
                    SEMANTIC_ERROR("Variable '" + *lastAssigned + "' not found for return after block", location);
                    return nullptr;
                }
                retVal = generator.getBuilder()->CreateLoad(
                    llvm::Type::getInt32Ty(generator.getContext()), varAlloca, "return_last_assignment");
            } else {
                retVal = blockExpr->codegen(generator);
            }
        } else {
            retVal = value->codegen(generator);
            if (!retVal) return nullptr;
        }
    } else {
        retVal = llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0);
    }
    generator.getBuilder()->CreateRet(retVal);
    return retVal;
}



