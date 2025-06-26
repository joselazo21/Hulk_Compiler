#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>


// BlockStatement implementation
BlockStatement::BlockStatement(const SourceLocation& loc) : Statement(loc) {}

BlockStatement::BlockStatement(const SourceLocation& loc, const std::vector<Statement*>& stmts) 
    : Statement(loc), statements(stmts) {}

BlockStatement::~BlockStatement() {
    for (Statement* stmt : statements) {
        delete stmt;
    }
}

void BlockStatement::addStatement(Statement* stmt) {
    statements.push_back(stmt);
}

const std::vector<Statement*>& BlockStatement::getStatements() const {
    return statements;
}

void BlockStatement::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── Block Statement\n";
    for (Statement* stmt : statements) {
        stmt->printNode(depth + 1);
    }
}

bool BlockStatement::Validate(IContext* context) {
    std::cout << "[LOG] BlockStatement::Validate, statements.size()=" << statements.size() << "\n";
    for (Statement* stmt : statements) {
        std::cout << "[LOG] BlockStatement::Validate, stmt=" << stmt << "\n";
        if (!stmt->Validate(context)) {
            std::cout << "[LOG] BlockStatement::Validate, stmt failed\n";
            return false;
        }
    }
    std::cout << "[LOG] BlockStatement::Validate, all statements validated\n";
    return true;
}

llvm::Value* BlockStatement::codegen(CodeGenerator& generator) {
    std::cout << "[LOG] BlockStatement::codegen START, statements.size()=" << statements.size() << "\n";
    llvm::Value* lastValue = nullptr;

    // First pass: generate all function declarations (solo la firma)
    for (Statement* stmt : statements) {
        std::cout << "[LOG] BlockStatement::codegen, first pass, stmt=" << stmt << "\n";
        if (dynamic_cast<FunctionDeclaration*>(stmt)) {
            std::cout << "Generating function declaration (signature only)\n";
            stmt->generateDeclarations(generator);
        }
    }

    // Second pass: generate all function bodies
    for (Statement* stmt : statements) {
        std::cout << "[LOG] BlockStatement::codegen, second pass, stmt=" << stmt << "\n";
        if (dynamic_cast<FunctionDeclaration*>(stmt)) {
            std::cout << "Generating function body\n";
            stmt->generateExecutableCode(generator, true);
        }
    }

    // Third pass: execute non-function statements
    Statement* lastNonFuncStmt = nullptr;
    for (Statement* stmt : statements) {
        std::cout << "[LOG] BlockStatement::codegen, third pass, stmt=" << stmt << "\n";
        if (dynamic_cast<FunctionDeclaration*>(stmt)) {
            continue;  // Skip functions on this pass
        }

        llvm::BasicBlock* currentBlock = generator.getBuilder()->GetInsertBlock();
        llvm::Function* currentFunction = currentBlock ? currentBlock->getParent() : nullptr;
        // Solo sal del bucle si el bloque tiene terminador Y estamos en main
        if (currentBlock && currentBlock->getTerminator() && currentFunction && currentFunction->getName() == "main") {
            std::cout << "Block " << currentBlock->getName().str() 
                     << " in main has terminator, breaking...\n";
            break;
        }
        // Si el bloque tiene terminador pero NO estamos en main, permite que otros bloques (como while.body) sigan generando código
        if (currentBlock && currentBlock->getTerminator()) {
            std::cout << "Block " << currentBlock->getName().str() << " has terminator, skipping...\n";
            continue;
        }

        std::cout << "Generating non-function statement\n";
        lastValue = stmt->codegen(generator);
        lastNonFuncStmt = stmt;
        // Si stmt->codegen generó un terminador, no generes más instrucciones
        llvm::BasicBlock* afterStmtBlock = generator.getBuilder()->GetInsertBlock();
        if (afterStmtBlock && afterStmtBlock->getTerminator()) {
            std::cout << "[LOG] BlockStatement::codegen, block has terminator after stmt, breaking\n";
            break;
        }
        if (!lastValue) {
            lastValue = llvm::ConstantInt::get(
                llvm::Type::getInt32Ty(generator.getContext()), 0);
        }
    }

    llvm::BasicBlock* currentBlock = generator.getBuilder()->GetInsertBlock();
    llvm::Function* currentFunction = currentBlock ? currentBlock->getParent() : nullptr;
    // SOLO inserta ret automático si estamos en el entry de main y no hay terminador
    if (currentFunction && currentFunction->getName() == "main" && 
        currentBlock && !currentBlock->getTerminator() && 
        &currentFunction->getEntryBlock() == currentBlock) {
        generator.getBuilder()->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0));
    }
    std::cout << "[LOG] BlockStatement::codegen END, returning " << lastValue << "\n";
    return lastValue;
}

// --- Implementations for codegen separation ---

void BlockStatement::generateDeclarations(CodeGenerator& cg) {
    for (auto* stmt : statements) {
        stmt->generateDeclarations(cg);
    }
}

llvm::Value* BlockStatement::generateExecutableCode(CodeGenerator& cg, bool onlyFunctions) {
    llvm::Value* lastValue = nullptr;
    
    if (onlyFunctions) {
        // First generate function declarations
        for (auto* stmt : statements) {
            if (dynamic_cast<FunctionDeclaration*>(stmt)) {
                stmt->generateDeclarations(cg);
            }
        }
        // Then generate function bodies
        for (auto* stmt : statements) {
            if (dynamic_cast<FunctionDeclaration*>(stmt)) {
                stmt->generateExecutableCode(cg, onlyFunctions);
            }
        }
    } else {
        // Execute non-function statements
        for (auto* stmt : statements) {
            if (!dynamic_cast<FunctionDeclaration*>(stmt)) {
                lastValue = stmt->generateExecutableCode(cg, onlyFunctions);
                
                // Check if block has terminator after statement
                llvm::BasicBlock* currentBlock = cg.getBuilder()->GetInsertBlock();
                if (currentBlock && currentBlock->getTerminator()) {
                    break;
                }
            }
        }
    }
    return lastValue;
}

Expression* BlockStatement::getLastExpression() const {
    // Find the last expression statement in the block
    for (auto it = statements.rbegin(); it != statements.rend(); ++it) {
        Statement* stmt = *it;
        
        // If it's an expression statement, return its expression
        if (auto exprStmt = dynamic_cast<ExpressionStatement*>(stmt)) {
            return exprStmt->getExpression();
        }
        
        // If it's a return statement, return its expression for type checking
        if (auto returnStmt = dynamic_cast<ReturnStatement*>(stmt)) {
            return returnStmt->getValue();
        }
        
        // Skip function declarations as they don't contribute to the return value
        if (dynamic_cast<FunctionDeclaration*>(stmt)) {
            continue;
        }
        
        // For ForStatement, we need to analyze its body to determine return type
        if (auto forStmt = dynamic_cast<ForStatement*>(stmt)) {
            // Get the last expression from the for loop body
            BlockStatement* forBody = forStmt->getBody();
            if (forBody) {
                return forBody->getLastExpression();
            }
            return nullptr;
        }
        
        // For IfStatement, we can't safely return it as an Expression
        // Instead, we'll return nullptr and let the caller handle this case
        if (dynamic_cast<IfStatement*>(stmt)) {
            // IfStatements can't be treated as expressions in this context
            // The type checker should handle function return type validation differently
            return nullptr;
        }
        
        // For other statement types, we can't extract an expression
        // This might need to be extended based on your language's semantics
    }
    
    return nullptr; // No expression found
}

Statement* BlockStatement::getLastStatement() const {
    // Find the last non-function-declaration statement in the block
    for (auto it = statements.rbegin(); it != statements.rend(); ++it) {
        Statement* stmt = *it;
        
        // Skip function declarations as they don't contribute to the return value
        if (dynamic_cast<FunctionDeclaration*>(stmt)) {
            continue;
        }
        
        return stmt;
    }
    
    return nullptr; // No statement found
}
