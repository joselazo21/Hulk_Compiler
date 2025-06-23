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
    bool hasErrors = false;
    
    // First validate all arguments
    for (Expression* arg : args) {
        if (!arg->Validate(context)) {
            hasErrors = true;
        }
    }
    
    // Permitir llamadas a métodos de objetos (ej: __iter_x.next())
    if (func_name.find('.') != std::string::npos) {
        // Considera válido cualquier llamada de la forma objeto.metodo(...)
        return !hasErrors;
    }
    
    // Check if function exists with correct arity
    if (!context->isDefined(func_name, args.size())) {
        SEMANTIC_ERROR("Function '" + func_name + "' not defined with " + 
                      std::to_string(args.size()) + " arguments", location);
        hasErrors = true;
    }
    
    // Create type registry and checker for strict type verification
    TypeRegistry typeRegistry(context);
    TypeChecker checker(typeRegistry);
    
    // Infer types of all arguments using the formal type system
    std::vector<const Type*> argTypes;
    std::vector<std::string> argTypeNames;
    
    for (size_t i = 0; i < args.size(); i++) {
        Expression* arg = args[i];
        
        // First try to get type directly from context for variables
        const Type* argType = nullptr;
        if (auto variable = dynamic_cast<Variable*>(arg)) {
            std::string varName = variable->getName();
            std::string typeName = context->getVariableTypeName(varName);
            if (!typeName.empty()) {
                argType = typeRegistry.getType(typeName);
            }
        }
        
        // If not found, use the formal type inference system
        if (!argType) {
            argType = checker.inferType(arg, context);
        }
        
        if (!argType) {
            SEMANTIC_ERROR("Cannot infer type for argument " + std::to_string(i) + 
                         " in call to function '" + func_name + "'", location);
            hasErrors = true;
        } else {
            argTypes.push_back(argType);
            argTypeNames.push_back(argType->toString());
            

        }
    }
    
    // Special handling for print function which accepts variable arguments
    if (func_name == "print") {
        // Print accepts any number of arguments of any type
        if (args.empty()) {
            SEMANTIC_ERROR("print requires at least one argument", location);
            hasErrors = true;
        }
        if (!hasErrors) {

        }
        return !hasErrors;
    }
    
    // Special handling for random function
    if (func_name == "random") {
        // Random accepts one argument (max value)
        if (args.size() != 1) {
            SEMANTIC_ERROR("random function expects exactly 1 argument", location);
            hasErrors = true;
        }
        if (!hasErrors) {

        }
        return !hasErrors;
    }
    
    // Special handling for rand function
    if (func_name == "rand") {
        // Rand accepts no arguments
        if (args.size() != 0) {
            SEMANTIC_ERROR("rand function expects no arguments", location);
            hasErrors = true;
        }
        if (!hasErrors) {

        }
        return !hasErrors;
    }
    
    // Get function type information from context
    std::cout << "[DEBUG] FunctionCall::Validate - Getting function type for '" << func_name << "'" << std::endl;
    const FunctionType* funcType = getFunctionType(func_name, context, typeRegistry);
    if (!funcType) {
        std::cout << "[DEBUG] FunctionCall::Validate - No function type found for '" << func_name << "'" << std::endl;
        // For other built-in functions, create a temporary function type or allow them
        if (isBuiltinFunction(func_name)) {

            return !hasErrors;
        }
        
        SEMANTIC_ERROR("Function '" + func_name + "' not found or has no type information", location);
        hasErrors = true;
        return !hasErrors;
    } else {
        std::cout << "[DEBUG] FunctionCall::Validate - Found function type for '" << func_name << "'" << std::endl;
    }
    
    // Verify argument count matches function signature
    if (funcType->getParamTypes().size() != argTypes.size()) {
        SEMANTIC_ERROR("Function '" + func_name + "' expects " + 
                      std::to_string(funcType->getParamTypes().size()) + 
                      " arguments, but " + std::to_string(argTypes.size()) + " provided", location);
        hasErrors = true;
    }
    
    // Verify type compatibility using the formal type checker (only if we have valid types)
    if (!argTypes.empty() && argTypes.size() == funcType->getParamTypes().size() && 
        !checker.checkFunctionCall(funcType, argTypes)) {
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
        hasErrors = true;
    }
    
    if (!hasErrors) {

    }
    return !hasErrors;
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

        // Para range, convierte float a i32 si es necesario
        if (func_name == "range") {
            if (argValue->getType()->isFloatTy()) {
                // Convert float to i32
                argValue = generator.getBuilder()->CreateFPToSI(argValue, llvm::Type::getInt32Ty(generator.getContext()), "float_to_int");
            } else if (!argValue->getType()->isIntegerTy(32)) {
                SEMANTIC_ERROR("Arguments to range must be integers or numbers", location);
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
    std::cout << "[DEBUG] getFunctionType - Looking up function '" << funcName << "'" << std::endl;
    
    // First try to get user-defined function information from context
    std::vector<std::string> paramTypeNames = context->getFunctionParamTypes(funcName);
    std::string returnTypeName = context->getFunctionReturnType(funcName);
    
    std::cout << "[DEBUG] getFunctionType - Found param types: [";
    for (size_t i = 0; i < paramTypeNames.size(); i++) {
        if (i > 0) std::cout << ", ";
        std::cout << paramTypeNames[i];
    }
    std::cout << "] -> " << returnTypeName << std::endl;
    
    if (!paramTypeNames.empty() || !returnTypeName.empty()) {
        // Convert string type names to Type objects
        std::vector<std::unique_ptr<Type>> paramTypes;
        
        for (const std::string& typeName : paramTypeNames) {
            std::cout << "[DEBUG] getFunctionType - Converting type name '" << typeName << "'" << std::endl;
            const Type* type = typeRegistry.getType(typeName);
            if (type) {
                std::cout << "[DEBUG] getFunctionType - Found type in registry for '" << typeName << "'" << std::endl;
                // Create a copy of the type based on what we found
                if (typeName == "Number") {
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER));
                } else if (typeName == "String") {
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::STRING));
                } else if (typeName == "Boolean") {
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::BOOLEAN));
                } else if (typeName == "Void") {
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::VOID));
                } else {
                    std::cout << "[DEBUG] getFunctionType - Creating user-defined type for '" << typeName << "'" << std::endl;
                    // Create a user-defined type for custom types
                    paramTypes.push_back(std::make_unique<UserDefinedType>(typeName));
                }
            } else {
                std::cout << "[DEBUG] getFunctionType - Type not found in registry for '" << typeName << "'" << std::endl;
                // For types not in registry, create appropriate type objects
                if (typeName == "Number") {
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER));
                } else if (typeName == "String") {
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::STRING));
                } else if (typeName == "Boolean") {
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::BOOLEAN));
                } else if (typeName == "Void") {
                    paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::VOID));
                } else {
                    std::cout << "[DEBUG] getFunctionType - Creating user-defined type for unregistered '" << typeName << "'" << std::endl;
                    // Create a user-defined type for custom types even if not registered
                    paramTypes.push_back(std::make_unique<UserDefinedType>(typeName));
                }
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
        } else if (!returnTypeName.empty()) {
            // Create a user-defined type for custom return types
            returnType = std::make_unique<UserDefinedType>(returnTypeName);
        } else {
            // Default to Number if no return type specified
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
    
    // Random function - one parameter (max value), returns Number
    if (funcName == "random") {
        std::vector<std::unique_ptr<Type>> paramTypes;
        paramTypes.push_back(std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER));
        auto returnType = std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER);
        return new FunctionType(std::move(paramTypes), std::move(returnType));
    }
    
    return nullptr;
}

// Helper function to check if a function is built-in
bool FunctionCall::isBuiltinFunction(const std::string& funcName) {
    static const std::set<std::string> builtins = {
        "print", "sqrt", "sin", "cos", "range", "rand", "random"
    };
    return builtins.find(funcName) != builtins.end();
}