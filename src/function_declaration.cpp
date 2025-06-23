#include "tree.hpp"
#include "type_system.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

FunctionDeclaration::FunctionDeclaration(const std::string& func_name, 
                                       const std::vector<std::string>& parameters, 
                                       BlockStatement* func_body)
    : Statement(SourceLocation()), name(func_name), params(parameters), body(func_body) {
    // Initialize with default types if no annotations provided
    paramTypes.resize(params.size(), "Number"); // Default to Number
    returnType = "Number"; // Default return type
}

FunctionDeclaration::FunctionDeclaration(const SourceLocation& loc, 
                                       const std::string& func_name, 
                                       const std::vector<std::string>& parameters, 
                                       BlockStatement* func_body)
    : Statement(loc), name(func_name), params(parameters), body(func_body) {
    // Initialize with default types if no annotations provided
    paramTypes.resize(params.size(), "Number"); // Default to Number
    returnType = "Number"; // Default return type
}

FunctionDeclaration::FunctionDeclaration(const SourceLocation& loc, 
                                       const std::string& func_name, 
                                       const std::vector<std::string>& parameters,
                                       const std::vector<std::string>& param_types,
                                       const std::string& return_type,
                                       BlockStatement* func_body)
    : Statement(loc), name(func_name), params(parameters), paramTypes(param_types), 
      returnType(return_type), body(func_body) {}

void FunctionDeclaration::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── Function Declaration: " << name << "\n";
    printIndent(depth);
    std::cout << "│   ├── Parameters: ";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << params[i];
    }
    std::cout << "\n";
    printIndent(depth);
    std::cout << "│   └── Body:\n";
    body->printNode(depth + 2);
}

bool FunctionDeclaration::Validate(IContext* context) {
    if (!context) {
        SEMANTIC_ERROR("Null context in function declaration", location);
        return false;
    }

    // --- REGISTRA LA FUNCIÓN ANTES DE VALIDAR EL CUERPO (si no está ya registrada) ---
    // Check if function is already registered (it might have been registered in Program::Validate)
    std::cout << "[DEBUG] FunctionDeclaration::Validate for function '" << name << "' with params: [";
    for (size_t i = 0; i < paramTypes.size(); i++) {
        if (i > 0) std::cout << ", ";
        std::cout << params[i] << ":" << paramTypes[i];
    }
    std::cout << "] -> " << returnType << std::endl;
    
    if (!context->isDefined(name, params.size())) {
        std::cout << "[DEBUG] Function not defined, adding to context" << std::endl;
        if (!context->addFunction(name, params, paramTypes, returnType)) {
            SEMANTIC_ERROR("Function '" + name + "' already defined", location);
            return false;
        }
    } else {
        std::cout << "[DEBUG] Function already defined, checking types" << std::endl;
        // Check if the existing function has the correct types
        std::vector<std::string> existingParamTypes = context->getFunctionParamTypes(name);
        std::string existingReturnType = context->getFunctionReturnType(name);
        std::cout << "[DEBUG] Existing function types: [";
        for (size_t i = 0; i < existingParamTypes.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << existingParamTypes[i];
        }
        std::cout << "] -> " << existingReturnType << std::endl;
    }

    // Create function context
    std::unique_ptr<IContext> functionContext(context->createChildContext());
    if (!functionContext) {
        SEMANTIC_ERROR("Failed to create function context", location);
        return false;
    }

    // Add parameters to context with their types
    for (size_t i = 0; i < params.size(); i++) {
        if (params[i].empty()) {
            SEMANTIC_ERROR("Empty parameter name", location);
            return false;
        }
        
        llvm::LLVMContext* llvmCtx = context->getLLVMContext();
        if (!llvmCtx) {
            SEMANTIC_ERROR("Null LLVMContext in function declaration", location);
            return false;
        }
        
        // Convert type annotation to LLVM type
        llvm::Type* paramType = nullptr;
        if (i < paramTypes.size()) {
            if (paramTypes[i] == "Number") {
                paramType = llvm::Type::getFloatTy(*llvmCtx);
            } else if (paramTypes[i] == "String") {
                paramType = llvm::Type::getInt8PtrTy(*llvmCtx);
            } else if (paramTypes[i] == "Boolean") {
                paramType = llvm::Type::getInt1Ty(*llvmCtx);
            } else {
                // Default to Number for unknown types
                paramType = llvm::Type::getFloatTy(*llvmCtx);
            }
        } else {
            // Default to Number if no type annotation
            paramType = llvm::Type::getFloatTy(*llvmCtx);
        }
        
        if (!functionContext->addVariable(params[i], paramType)) {
            SEMANTIC_ERROR("Duplicate parameter: " + params[i], location);
            return false;
        }
    }

    // Validate body
    if (!body) {
        SEMANTIC_ERROR("Function has no body", location);
        return false;
    }

    bool bodyValid = body->Validate(functionContext.get());
    
    // Always validate the return type, even if there were errors in the body
    // This ensures we catch return type mismatches in addition to other errors
    bool returnTypeValid = true;
    
    // Get the last expression from the body to check its type
    Expression* lastExpr = body->getLastExpression();

    
    if (lastExpr) {

        
        // Create type registry and checker for type inference
        TypeRegistry typeRegistry(functionContext.get());
        TypeChecker checker(typeRegistry);
        
        // Infer the type of the last expression
        const Type* actualReturnType = checker.inferType(lastExpr, functionContext.get());
        
        if (actualReturnType) {
            std::string actualTypeName = actualReturnType->toString();

            
            // Check if the actual return type matches the declared return type
            if (actualTypeName != returnType) {
                SEMANTIC_ERROR("Function '" + name + "' declared to return " + returnType + 
                             " but actually returns " + actualTypeName, location);
                returnTypeValid = false;
            } else {

            }
        } else {
            // Could not infer the return type - this might be an error in the expression

            SEMANTIC_ERROR("Cannot determine return type of function '" + name + "'", location);
            returnTypeValid = false;
        }
    } else {

    }

    // Return true only if both body validation and return type validation succeeded
    return bodyValid && returnTypeValid;
}

FunctionDeclaration::~FunctionDeclaration() {
    delete body;
}

void FunctionDeclaration::generateDeclarations(CodeGenerator& cg) {
    // Solo declara la firma si no existe
    llvm::Function* function = cg.getModule()->getFunction(name);
    if (!function) {
        // For functions that work with vectors/iterables, use range pointer type
        // This is a heuristic based on common parameter names
        std::vector<llvm::Type*> argTypes;
        for (size_t i = 0; i < params.size(); i++) {
            const auto& param = params[i];
            if (param == "numbers" || param == "items" || param == "list" || param == "array" || param == "vector") {
                // Use range pointer type for vector-like parameters
                llvm::StructType* rangeType = llvm::StructType::getTypeByName(cg.getContext(), "struct.range");
                if (!rangeType) {
                    std::vector<llvm::Type*> members = {
                        llvm::Type::getInt32Ty(cg.getContext()), // current
                        llvm::Type::getInt32Ty(cg.getContext())  // end
                    };
                    rangeType = llvm::StructType::create(cg.getContext(), members, "struct.range");
                }
                argTypes.push_back(rangeType->getPointerTo());
            } else {
                // Use parameter type annotation if available
                llvm::Type* paramType = llvm::Type::getFloatTy(cg.getContext()); // Default to Number
                if (i < paramTypes.size()) {
                    if (paramTypes[i] == "String") {
                        paramType = llvm::Type::getInt8PtrTy(cg.getContext());
                    } else if (paramTypes[i] == "Boolean") {
                        paramType = llvm::Type::getInt1Ty(cg.getContext());
                    } else if (paramTypes[i] == "Number") {
                        paramType = llvm::Type::getFloatTy(cg.getContext());
                    }
                }
                argTypes.push_back(paramType);
            }
        }
        
        // Determine return type from annotation
        llvm::Type* retType = llvm::Type::getFloatTy(cg.getContext()); // Default to Number
        if (returnType == "String") {
            retType = llvm::Type::getInt8PtrTy(cg.getContext());
        } else if (returnType == "Boolean") {
            retType = llvm::Type::getInt1Ty(cg.getContext());
        } else if (returnType == "Number") {
            retType = llvm::Type::getFloatTy(cg.getContext());
        }
        
        llvm::FunctionType* functionType = llvm::FunctionType::get(
            retType,
            argTypes,
            false
        );
        function = llvm::Function::Create(
            functionType,
            llvm::Function::ExternalLinkage,
            name,
            cg.getModule()
        );
        // Set argument names
        unsigned idx = 0;
        for (auto &arg : function->args()) {
            arg.setName(params[idx++]);
        }
    }
}

llvm::Value* FunctionDeclaration::generateExecutableCode(CodeGenerator& cg, bool onlyFunctions) {
    // Solo genera el cuerpo si onlyFunctions es true o si no es función (por compatibilidad)
    if (!onlyFunctions) return nullptr;

    llvm::Function* function = cg.getModule()->getFunction(name);
    if (!function) return nullptr;

    // Si ya tiene bloques (más de solo el entry), no lo generes de nuevo
    if (!function->empty()) return function;

    llvm::BasicBlock* bb = llvm::BasicBlock::Create(
        cg.getContext(),
        "entry",
        function
    );
    cg.getBuilder()->SetInsertPoint(bb);

    cg.pushScope();

    for (auto &arg : function->args()) {
        llvm::AllocaInst* alloca = cg.createEntryBlockAlloca(
            function,
            arg.getName().str(),
            arg.getType()
        );
        cg.getBuilder()->CreateStore(&arg, alloca);
        cg.setNamedValue(arg.getName().str(), alloca);
    }

    llvm::Value* retVal = body->codegen(cg);

    // Add terminators to blocks that need them
    for (auto& block : *function) {
        if (!block.getTerminator()) {
            std::string blockName = block.getName().str();
            cg.getBuilder()->SetInsertPoint(&block);
            
            // Add return statement for all blocks that need terminators
            if (returnType == "String") {
                cg.getBuilder()->CreateRet(
                    llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(cg.getContext())));
            } else if (returnType == "Boolean") {
                cg.getBuilder()->CreateRet(
                    llvm::ConstantInt::get(llvm::Type::getInt1Ty(cg.getContext()), 0));
            } else {
                // Default to Number (float) - don't use retVal which might be i32
                cg.getBuilder()->CreateRet(
                    llvm::ConstantFP::get(llvm::Type::getFloatTy(cg.getContext()), 0.0f));
            }
        }
    }

    if (llvm::verifyFunction(*function, &llvm::errs())) {
        function->eraseFromParent();
        SEMANTIC_ERROR("Error in function generation", location);
        cg.popScope();
        return nullptr;
    }

    cg.popScope();
    return function;
}


llvm::Value* FunctionDeclaration::codegen(CodeGenerator& generator) {
    // Check if function already exists in module
    llvm::Function* function = generator.getModule()->getFunction(name);
    
    if (!function) {
        // Use the same logic as generateDeclarations for parameter types
        std::vector<llvm::Type*> argTypes;
        for (size_t i = 0; i < params.size(); i++) {
            const auto& param = params[i];
            if (param == "numbers" || param == "items" || param == "list" || param == "array" || param == "vector") {
                // Use range pointer type for vector-like parameters
                llvm::StructType* rangeType = llvm::StructType::getTypeByName(generator.getContext(), "struct.range");
                if (!rangeType) {
                    std::vector<llvm::Type*> members = {
                        llvm::Type::getInt32Ty(generator.getContext()), // current
                        llvm::Type::getInt32Ty(generator.getContext())  // end
                    };
                    rangeType = llvm::StructType::create(generator.getContext(), members, "struct.range");
                }
                argTypes.push_back(rangeType->getPointerTo());
            } else {
                // Use parameter type annotation if available
                llvm::Type* paramType = llvm::Type::getFloatTy(generator.getContext()); // Default to Number
                if (i < paramTypes.size()) {
                    if (paramTypes[i] == "String") {
                        paramType = llvm::Type::getInt8PtrTy(generator.getContext());
                    } else if (paramTypes[i] == "Boolean") {
                        paramType = llvm::Type::getInt1Ty(generator.getContext());
                    } else if (paramTypes[i] == "Number") {
                        paramType = llvm::Type::getFloatTy(generator.getContext());
                    }
                }
                argTypes.push_back(paramType);
            }
        }
        
        // Determine return type from annotation
        llvm::Type* retType = llvm::Type::getFloatTy(generator.getContext()); // Default to Number
        if (returnType == "String") {
            retType = llvm::Type::getInt8PtrTy(generator.getContext());
        } else if (returnType == "Boolean") {
            retType = llvm::Type::getInt1Ty(generator.getContext());
        } else if (returnType == "Number") {
            retType = llvm::Type::getFloatTy(generator.getContext());
        }
        
        llvm::FunctionType* functionType = llvm::FunctionType::get(
            retType,
            argTypes,
            false
        );

        function = llvm::Function::Create(
            functionType,
            llvm::Function::ExternalLinkage,
            name,
            generator.getModule()
        );
    }

    // Set arguments names
    unsigned idx = 0;
    for (auto &arg : function->args()) {
        arg.setName(params[idx++]);
    }

    std::cout << "Generating function: " << name << "\n";

    // Create basic block
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(
        generator.getContext(),
        "entry",
        function
    );

    std::cout << "Generating function2: " << name << "\n";

    generator.getBuilder()->SetInsertPoint(bb);

    std::cout << "Generating function3: " << name << "\n";

    // Create new scope
    generator.pushScope();

    // Create allocas for arguments
    for (auto &arg : function->args()) {
        llvm::AllocaInst* alloca = generator.createEntryBlockAlloca(
            function,
            arg.getName().str(),
            arg.getType()
        );
        generator.getBuilder()->CreateStore(&arg, alloca);
        generator.setNamedValue(arg.getName().str(), alloca);
    }

    // Generate function body
    llvm::Value* retVal = body->codegen(generator);
    
    // Add terminators to blocks that need them
    for (auto& block : *function) {
        if (!block.getTerminator()) {
            std::string blockName = block.getName().str();
            generator.getBuilder()->SetInsertPoint(&block);
            
            // Add return statement for all blocks that need terminators
            if (returnType == "String") {
                generator.getBuilder()->CreateRet(
                    llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(generator.getContext())));
            } else if (returnType == "Boolean") {
                generator.getBuilder()->CreateRet(
                    llvm::ConstantInt::get(llvm::Type::getInt1Ty(generator.getContext()), 0));
            } else {
                // Default to Number (float) - don't use retVal which might be i32
                generator.getBuilder()->CreateRet(
                    llvm::ConstantFP::get(llvm::Type::getFloatTy(generator.getContext()), 0.0f));
            }
        }
    }

    // Validate the generated function
    if (llvm::verifyFunction(*function, &llvm::errs())) {
        function->eraseFromParent();
        SEMANTIC_ERROR("Error in function generation", location);
        return nullptr;
    }

    std::cout << "Generating function7: " << name << "\n";

    generator.popScope();
    return function;
}
