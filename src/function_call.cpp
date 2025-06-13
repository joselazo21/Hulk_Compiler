#include "tree.hpp"
#include "type_system.hpp"
#include <sstream>
#include <iostream>
#include <set>
#include <error_handler.hpp>


FunctionCall::FunctionCall(const std::string& name, const std::vector<Expression*>& args)
    : Expression(SourceLocation()), func_name(name), args(args) {}

FunctionCall::FunctionCall(const SourceLocation& loc, const std::string& name, 
                         const std::vector<Expression*>& args)
    : Expression(loc), func_name(name), args(args) {}

std::string FunctionCall::toString() {
    std::string result = func_name + "(";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) result += ", ";
        result += args[i]->toString();
    }
    result += ")";
    return result;
}

void FunctionCall::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── FunctionCall: " << func_name << "\n";
    for (Expression* arg : args) {
        arg->printNode(depth + 1);
    }
}

bool FunctionCall::Validate(IContext* context) {
    // First validate all arguments
    for (Expression* arg : args) {
        if (!arg->Validate(context)) {
            return false;
        }
    }
    
    // Permitir llamadas a métodos de objetos (ej: __iter_x.next())
    if (func_name.find('.') != std::string::npos) {
        // Considera válido cualquier llamada de la forma objeto.metodo(...)
        return true;
    }
    
    // Check if function exists with correct arity
    if (!context->isDefined(func_name, args.size())) {
        SEMANTIC_ERROR("Function '" + func_name + "' not defined with " + 
                      std::to_string(args.size()) + " arguments", location);
        return false;
    }
    
    // Create type registry and checker for strict type verification
    TypeRegistry typeRegistry;
    TypeChecker checker(typeRegistry);
    
    // Infer types of all arguments using the formal type system
    std::vector<const Type*> argTypes;
    std::vector<std::string> argTypeNames;
    
    for (size_t i = 0; i < args.size(); i++) {
        Expression* arg = args[i];
        
        // Use the formal type inference system
        const Type* argType = checker.inferType(arg, context);
        if (!argType) {
            SEMANTIC_ERROR("Cannot infer type for argument " + std::to_string(i) + 
                         " in call to function '" + func_name + "'", location);
            return false;
        }
        
        argTypes.push_back(argType);
        argTypeNames.push_back(argType->toString());
        
        std::cout << "[DEBUG] Argument " << i << " inferred type: " << argType->toString() << std::endl;
    }
    
    // Special handling for print function which accepts variable arguments
    if (func_name == "print") {
        // Print accepts any number of arguments of any type
        if (args.empty()) {
            SEMANTIC_ERROR("print requires at least one argument", location);
            return false;
        }
        std::cout << "[DEBUG] Function print validated with " << args.size() << " arguments" << std::endl;
        return true;
    }
    
    // Get function type information from context
    const FunctionType* funcType = getFunctionType(func_name, context, typeRegistry);
    if (!funcType) {
        // For other built-in functions, create a temporary function type or allow them
        if (isBuiltinFunction(func_name)) {
            std::cout << "[DEBUG] Function " << func_name << " is built-in, allowing call" << std::endl;
            return true;
        }
        
        SEMANTIC_ERROR("Function '" + func_name + "' not found or has no type information", location);
        return false;
    }
    
    // Verify argument count matches function signature
    if (funcType->getParamTypes().size() != argTypes.size()) {
        SEMANTIC_ERROR("Function '" + func_name + "' expects " + 
                      std::to_string(funcType->getParamTypes().size()) + 
                      " arguments, but " + std::to_string(argTypes.size()) + " provided", location);
        return false;
    }
    
    // Verify type compatibility using the formal type checker
    if (!checker.checkFunctionCall(funcType, argTypes)) {
        std::string errorMsg = "Type mismatch in call to function '" + func_name + "'. Expected: (";
        for (size_t i = 0; i < funcType->getParamTypes().size(); i++) {
            if (i > 0) errorMsg += ", ";
            errorMsg += funcType->getParamTypes()[i]->toString();
        }
        errorMsg += "), got: (";
        for (size_t i = 0; i < argTypeNames.size(); i++) {
            if (i > 0) errorMsg += ", ";
            errorMsg += argTypeNames[i];
        }
        errorMsg += ")";
        SEMANTIC_ERROR(errorMsg, location);
        return false;
    }
    
    std::cout << "[DEBUG] Function call " << func_name << " type check passed" << std::endl;
    return true;
}

FunctionCall::~FunctionCall() {
    for (Expression* arg : args) {
        delete arg;
    }
}

llvm::Value* FunctionCall::codegen(CodeGenerator& generator) {
    // --- ESPECIAL: Si es "range", declara la función si no existe ---
    if (func_name == "range") {
        llvm::LLVMContext& ctx = generator.getContext();
        llvm::Module* module = generator.getModule();

        // Declara struct.range si no existe
        llvm::StructType* rangeStruct = llvm::StructType::getTypeByName(ctx, "struct.range");
        if (!rangeStruct) {
            std::vector<llvm::Type*> members = {
                llvm::Type::getInt32Ty(ctx), // current
                llvm::Type::getInt32Ty(ctx)  // end
            };
            rangeStruct = llvm::StructType::create(ctx, members, "struct.range");
        }
        llvm::Type* retType = rangeStruct->getPointerTo();

        // Declara la función range si no existe
        llvm::Function* rangeFunc = module->getFunction("range");
        if (!rangeFunc) {
            std::vector<llvm::Type*> argTypes = {
                llvm::Type::getInt32Ty(ctx),
                llvm::Type::getInt32Ty(ctx)
            };
            llvm::FunctionType* fnType = llvm::FunctionType::get(retType, argTypes, false);
            rangeFunc = llvm::Function::Create(
                fnType,
                llvm::Function::ExternalLinkage,
                "range",
                module
            );
            rangeFunc->arg_begin()->setName("start");
            (rangeFunc->arg_begin() + 1)->setName("end");
        }
    }

    // --- ESPECIAL: Métodos de iterador (__iter_x.next, __iter_x.current) ---
    // Las funciones de iterador ahora se generan en implementIteratorFunctions
    // y se llaman cuando se declara el iterador en LetIn::codegen

    // For math functions, we'll use the standard library functions with float arguments
    std::string actualFuncName = func_name;
    if (func_name == "sqrt" || func_name == "sin" || func_name == "cos") {
        // Use standard library math functions that work with float/double
        actualFuncName = func_name;
    }

    llvm::Function* calleeF = generator.getModule()->getFunction(actualFuncName);
    if (!calleeF) {
        SEMANTIC_ERROR("Unknown function referenced: " + func_name, location);
        return nullptr;
    }

    // Verify argument count
    if (calleeF->arg_size() != args.size()) {
        SEMANTIC_ERROR("Incorrect # arguments passed to function " + func_name, location);
        return nullptr;
    }

    std::vector<llvm::Value*> argsV;
    for (unsigned i = 0; i < args.size(); ++i) {
        llvm::Value* argValue = args[i]->codegen(generator);
        if (!argValue) return nullptr;

        // Para range, permite solo i32
        if (func_name == "range") {
            if (!argValue->getType()->isIntegerTy(32)) {
                SEMANTIC_ERROR("Arguments to range must be integers", location);
                return nullptr;
            }
        } else if (func_name == "sqrt" || func_name == "sin" || func_name == "cos" || func_name == "pow") {
            // For math functions, convert arguments to double
            if (argValue->getType()->isFloatTy()) {
                argValue = generator.getBuilder()->CreateFPExt(argValue, llvm::Type::getDoubleTy(generator.getContext()), "ext_to_double");
            } else if (argValue->getType()->isIntegerTy()) {
                argValue = generator.getBuilder()->CreateSIToFP(argValue, llvm::Type::getDoubleTy(generator.getContext()), "int_to_double");
            }
        } else {
            // Ensure argument type matches parameter type
            if (calleeF->arg_size() > i) {
                llvm::Type* expectedType = calleeF->getArg(i)->getType();
                llvm::Type* actualType = argValue->getType();
                
                // Allow range pointer to range pointer conversion
                if (expectedType->isPointerTy() && actualType->isPointerTy()) {
                    llvm::Type* expectedPointeeType = expectedType->getPointerElementType();
                    llvm::Type* actualPointeeType = actualType->getPointerElementType();
                    
                    // Check if both are struct types with the same name
                    if (expectedPointeeType->isStructTy() && actualPointeeType->isStructTy()) {
                        llvm::StructType* expectedStruct = llvm::cast<llvm::StructType>(expectedPointeeType);
                        llvm::StructType* actualStruct = llvm::cast<llvm::StructType>(actualPointeeType);
                        
                        // Allow if both are range structs (even if they have different internal representations)
                        if (expectedStruct->getName().startswith("struct.range") && 
                            actualStruct->getName().startswith("struct.range")) {
                            // Types are compatible, continue
                        } else if (actualType != expectedType) {
                            SEMANTIC_ERROR("Argument type mismatch in call to " + func_name, location);
                            return nullptr;
                        }
                    } else if (actualType != expectedType) {
                        SEMANTIC_ERROR("Argument type mismatch in call to " + func_name, location);
                        return nullptr;
                    }
                } else if (actualType != expectedType) {
                    SEMANTIC_ERROR("Argument type mismatch in call to " + func_name, location);
                    return nullptr;
                }
            }
        }
        argsV.push_back(argValue);
    }

    llvm::Value* result = generator.getBuilder()->CreateCall(calleeF, argsV, "calltmp");
    
    // For math functions, convert result back to float if needed
    if ((func_name == "sqrt" || func_name == "sin" || func_name == "cos" || func_name == "pow") && 
        result && result->getType()->isDoubleTy()) {
        result = generator.getBuilder()->CreateFPTrunc(result, llvm::Type::getFloatTy(generator.getContext()), "trunc_to_float");
    }
    
    return result;
}

// Helper function to get function type from context
const FunctionType* FunctionCall::getFunctionType(const std::string& funcName, IContext* context, TypeRegistry& typeRegistry) {
    // First try to get user-defined function information from context
    std::vector<std::string> paramTypeNames = context->getFunctionParamTypes(funcName);
    std::string returnTypeName = context->getFunctionReturnType(funcName);
    
    if (!paramTypeNames.empty() || !returnTypeName.empty()) {
        // Convert string type names to Type objects
        std::vector<std::unique_ptr<Type>> paramTypes;
        
        for (const std::string& typeName : paramTypeNames) {
            const Type* type = typeRegistry.getType(typeName);
            if (type) {
                // Create a copy of the type (simplified - in real implementation, use proper cloning)
                if (typeName == "Number") {
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER));
                } else if (typeName == "String") {
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::STRING));
                } else if (typeName == "Boolean") {
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::BOOLEAN));
                } else if (typeName == "Void") {
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::VOID));
                } else {
                    // Default to Number for unknown types
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER));
                }
            } else {
                // Default to Number if type not found
                paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER));
            }
        }
        
        // Create return type
        std::unique_ptr<Type> returnType;
        if (returnTypeName == "Number") {
            returnType = std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER);
        } else if (returnTypeName == "String") {
            returnType = std::make_unique<BuiltinTypeImpl>(BuiltinType::STRING);
        } else if (returnTypeName == "Boolean") {
            returnType = std::make_unique<BuiltinTypeImpl>(BuiltinType::BOOLEAN);
        } else if (returnTypeName == "Void") {
            returnType = std::make_unique<BuiltinTypeImpl>(BuiltinType::VOID);
        } else {
            // Default to Number
            returnType = std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER);
        }
        
        return new FunctionType(std::move(paramTypes), std::move(returnType));
    }
    
    // Fall back to built-in function types
    
    // Built-in math functions
    if (funcName == "sqrt" || funcName == "sin" || funcName == "cos") {
        std::vector<std::unique_ptr<Type>> paramTypes;
        paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER));
        auto returnType = std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER);
        return new FunctionType(std::move(paramTypes), std::move(returnType));
    }
    
    // Range function
    if (funcName == "range") {
        std::vector<std::unique_ptr<Type>> paramTypes;
        paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER));
        paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER));
        auto returnType = std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER); // Simplified - should be iterator type
        return new FunctionType(std::move(paramTypes), std::move(returnType));
    }
    
    // Print function - accepts any number of arguments, returns void
    if (funcName == "print") {
        std::vector<std::unique_ptr<Type>> paramTypes;
        // For print, we'll allow any single argument for now
        paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::STRING));
        auto returnType = std::make_unique<BuiltinTypeImpl>(BuiltinType::VOID);
        return new FunctionType(std::move(paramTypes), std::move(returnType));
    }
    
    // Rand function - no parameters, returns Number
    if (funcName == "rand") {
        std::vector<std::unique_ptr<Type>> paramTypes;
        // No parameters for rand()
        auto returnType = std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER);
        return new FunctionType(std::move(paramTypes), std::move(returnType));
    }
    
    return nullptr;
}

// Helper function to check if a function is built-in
bool FunctionCall::isBuiltinFunction(const std::string& funcName) {
    static const std::set<std::string> builtins = {
        "print", "sqrt", "sin", "cos", "range", "rand"
    };
    return builtins.find(funcName) != builtins.end();
}