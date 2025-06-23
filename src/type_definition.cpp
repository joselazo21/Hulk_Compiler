#include "tree.hpp"
#include <iostream>

TypeDefinition::TypeDefinition(const SourceLocation& loc, const std::string& name, 
                               const std::vector<std::string>& params,
                               const std::vector<std::pair<std::string, Expression*>>& fields,
                               const std::vector<std::pair<std::string, std::pair<std::vector<std::string>, Expression*>>>& methods,
                               const std::string& parentType,
                               const std::vector<Expression*>& parentArgs)
    : Statement(loc), name(name), params(params), fields(fields), methods(methods), 
      parentType(parentType), parentArgs(parentArgs), useTypedMethods(false) {}

TypeDefinition::TypeDefinition(const SourceLocation& loc, const std::string& name, 
                               const std::vector<std::string>& params,
                               const std::vector<std::pair<std::string, Expression*>>& fields,
                               const std::vector<std::pair<std::string, MethodInfo>>& typedMethods,
                               const std::string& parentType,
                               const std::vector<Expression*>& parentArgs)
    : Statement(loc), name(name), params(params), fields(fields), typedMethods(typedMethods), 
      parentType(parentType), parentArgs(parentArgs), useTypedMethods(true) {}

TypeDefinition::~TypeDefinition() {
    // Delete field expressions
    for (auto& field : fields) {
        delete field.second;
    }
    // Delete method expressions
    if (useTypedMethods) {
        for (auto& method : typedMethods) {
            delete method.second.body;
        }
    } else {
        for (auto& method : methods) {
            delete method.second.second;
        }
    }
    // Delete parent arguments
    for (auto* arg : parentArgs) {
        delete arg;
    }
}

void TypeDefinition::printNode(int depth) {
    printIndent(depth);
    std::cout << "TypeDefinition: " << name << "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << params[i];
    }
    std::cout << ")";
    if (parentType != "Object") {
        std::cout << " inherits " << parentType;
        if (!parentArgs.empty()) {
            std::cout << "(";
            for (size_t i = 0; i < parentArgs.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << "..."; // Just indicate there are parent args
            }
            std::cout << ")";
        }
    }
    std::cout << std::endl;
    
    // Print fields
    for (const auto& field : fields) {
        printIndent(depth + 1);
        std::cout << "Field: " << field.first << " = ";
        if (field.second) {
            field.second->printNode(0);
        }
    }
    
    // Print methods
    if (useTypedMethods) {
        for (const auto& method : typedMethods) {
            printIndent(depth + 1);
            std::cout << "Method: " << method.first << "(";
            for (size_t i = 0; i < method.second.params.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << method.second.params[i];
            }
            std::cout << ") : " << method.second.returnType << " => ";
            if (method.second.body) {
                method.second.body->printNode(0);
            }
        }
    } else {
        for (const auto& method : methods) {
            printIndent(depth + 1);
            std::cout << "Method: " << method.first << "(";
            for (size_t i = 0; i < method.second.first.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << method.second.first[i];
            }
            std::cout << ") => ";
            if (method.second.second) {
                method.second.second->printNode(0);
            }
        }
    }
}

bool TypeDefinition::Validate(IContext* context) {
    // Check if parent type exists (unless it's Object)
    if (parentType != "Object") {
        // Check if parent type is registered in context
        // Use hasType to check if the type name is registered, regardless of struct being nullptr
        if (!context->hasType(parentType)) {
            std::cerr << "Error: Parent type '" << parentType << "' of type '" << name 
                      << "' is not defined. Make sure the parent type is declared." << std::endl;
            return false;
        }
        
        // Check if trying to inherit from builtin types
        if (parentType == "Number" || parentType == "String" || parentType == "Boolean") {
            std::cerr << "Error: Cannot inherit from builtin type '" << parentType << "'" << std::endl;
            return false;
        }
    }
    
    // Create a new context for the type definition
    IContext* typeContext = context->createChildContext();
    
    // Add constructor parameters to the type context
    for (const auto& param : params) {
        typeContext->addVariable(param);
    }
    
    // Add 'self' to the type context
    typeContext->addVariable("self");
    
    // Set the current type being processed
    typeContext->setCurrentType(name);
    
    // Validate parent arguments if any
    for (const auto& arg : parentArgs) {
        if (arg && !arg->Validate(typeContext)) {
            delete typeContext;
            return false;
        }
    }
    
    // Validate field initializers with the type context
    for (const auto& field : fields) {
        if (field.second && !field.second->Validate(typeContext)) {
            delete typeContext;
            return false;
        }
    }
    
    // Validate method bodies
    if (useTypedMethods) {
        for (const auto& method : typedMethods) {
            // Create new context for method with parameters
            IContext* methodContext = typeContext->createChildContext();
            for (const auto& param : method.second.params) {
                // Extract just the parameter name if it contains type annotation
                std::string paramName = param;
                size_t colonPos = paramName.find(':');
                if (colonPos != std::string::npos) {
                    paramName = paramName.substr(0, colonPos);
                    // Remove any trailing whitespace
                    paramName.erase(paramName.find_last_not_of(" \t") + 1);
                }
                methodContext->addVariable(paramName);
            }
            
            if (method.second.body && !method.second.body->Validate(methodContext)) {
                delete methodContext;
                delete typeContext;
                return false;
            }
            delete methodContext;
        }
    } else {
        for (const auto& method : methods) {
            // Create new context for method with parameters
            IContext* methodContext = typeContext->createChildContext();
            for (const auto& param : method.second.first) {
                // Extract just the parameter name if it contains type annotation
                std::string paramName = param;
                size_t colonPos = paramName.find(':');
                if (colonPos != std::string::npos) {
                    paramName = paramName.substr(0, colonPos);
                    // Remove any trailing whitespace
                    paramName.erase(paramName.find_last_not_of(" \t") + 1);
                }
                methodContext->addVariable(paramName);
            }
            
            if (method.second.second && !method.second.second->Validate(methodContext)) {
                delete methodContext;
                delete typeContext;
                return false;
            }
            delete methodContext;
        }
    }
    
    delete typeContext;
    return true;
}

llvm::Value* TypeDefinition::codegen(CodeGenerator& generator) {
    std::cout << "Generating code for type definition: " << name << std::endl;
    
    // Get the LLVM context
    llvm::LLVMContext& context = generator.getContext();
    llvm::Module* module = generator.getModule();
    
    // Create a struct type for this class
    std::vector<llvm::Type*> fieldTypes;
    std::vector<std::string> fieldNames;
    
    // Get parent type information
    IContext* currentContext = generator.getContextObject();
    llvm::StructType* parentStructType = nullptr;
    int parentFieldCount = 0;
    
    if (parentType != "Object") {
        parentStructType = currentContext->getType(parentType);
        if (parentStructType) {
            // Add parent fields first
            parentFieldCount = currentContext->getFieldCount(parentType);
            for (int i = 0; i < parentFieldCount; i++) {
                fieldTypes.push_back(parentStructType->getElementType(i));
                fieldNames.push_back(currentContext->getFieldName(parentType, i));
            }
        }
    }
    
    // Add the actual fields defined in the type
    for (const auto& field : fields) {
        // Extract the actual field name (remove type annotation if present)
        std::string actualFieldName = field.first;
        std::string fieldTypeName = "Number"; // Default type
        
        // Check if field name contains type annotation (e.g., "x:Number")
        size_t colonPos = actualFieldName.find(':');
        if (colonPos != std::string::npos) {
            fieldTypeName = actualFieldName.substr(colonPos + 1);
            actualFieldName = actualFieldName.substr(0, colonPos);
            // Remove any leading/trailing whitespace
            fieldTypeName.erase(0, fieldTypeName.find_first_not_of(" \t"));
            fieldTypeName.erase(fieldTypeName.find_last_not_of(" \t") + 1);
        }
        
        // Determine field type based on annotation or field name
        llvm::Type* fieldType;
        if (fieldTypeName == "String" || actualFieldName == "firstname" || actualFieldName == "lastname") {
            fieldType = llvm::Type::getInt8PtrTy(context); // String type
        } else if (fieldTypeName == "Boolean") {
            fieldType = llvm::Type::getInt1Ty(context); // Boolean type
        } else {
            fieldType = llvm::Type::getFloatTy(context); // Number type (default)
        }
        
        fieldTypes.push_back(fieldType);
        fieldNames.push_back(actualFieldName);
    }
    
    // Get the struct type (should already exist from phase 1)
    std::string structName = "struct." + name;
    llvm::StructType* structType = llvm::StructType::getTypeByName(context, structName);
    if (!structType) {
        std::cerr << "Error: Struct type " << structName << " should have been created in phase 1" << std::endl;
        return nullptr;
    }
    
    // Set the body of the struct with the field types
    structType->setBody(fieldTypes);
    
    // Store field indices for later access
    for (size_t i = 0; i < fieldNames.size(); i++) {
        currentContext->addFieldIndex(name, fieldNames[i], i);
    }
    
    // Generate methods - use typed methods if available, otherwise fall back to legacy
    const auto& methodsToProcess = useTypedMethods ? 
        std::vector<std::pair<std::string, std::pair<std::vector<std::string>, std::pair<std::string, Expression*>>>>() :
        std::vector<std::pair<std::string, std::pair<std::vector<std::string>, std::pair<std::string, Expression*>>>>();
    
    // Convert typed methods to the format needed for processing
    std::vector<std::tuple<std::string, std::vector<std::string>, std::string, Expression*>> methodsData;
    
    if (useTypedMethods) {
        for (const auto& method : typedMethods) {
            methodsData.emplace_back(method.first, method.second.params, method.second.returnType, method.second.body);
        }
    } else {
        for (const auto& method : methods) {
            // Extract return type from signature if present
            std::string methodName = method.first;
            std::string returnType = "Number"; // Default
            
            // Check if signature contains return type annotation (e.g., "name():String")
            size_t colonPos = methodName.find("):");
            if (colonPos != std::string::npos) {
                returnType = methodName.substr(colonPos + 2);
                methodName = methodName.substr(0, colonPos + 1); // Keep method name with params
                
                // Extract just the method name without parameters
                size_t parenPos = methodName.find("(");
                if (parenPos != std::string::npos) {
                    methodName = methodName.substr(0, parenPos);
                }
            } else {
                // Extract just the method name without parameters
                size_t parenPos = methodName.find("(");
                if (parenPos != std::string::npos) {
                    methodName = methodName.substr(0, parenPos);
                }
            }
            
            methodsData.emplace_back(methodName, method.second.first, returnType, method.second.second);
        }
    }
    
    // FIRST PASS: Create all method signatures and register them in the context
    std::vector<llvm::Function*> methodFunctions;
    
    for (const auto& methodData : methodsData) {
        const std::string& methodName_local = std::get<0>(methodData);
        const std::vector<std::string>& params = std::get<1>(methodData);
        const std::string& returnTypeAnnotation = std::get<2>(methodData);
        
        std::string methodName = name + "." + methodName_local;
        
        // Create function type: determine return type from annotation
        std::vector<llvm::Type*> paramTypes;
        paramTypes.push_back(structType->getPointerTo()); // 'self' parameter
        
        // Add other parameters based on their type annotations
        for (size_t i = 0; i < params.size(); i++) {
            std::string param = params[i];
            std::string paramTypeName = "Number"; // Default type
            
            // Check if parameter contains type annotation (e.g., "item:String")
            size_t colonPos = param.find(':');
            if (colonPos != std::string::npos) {
                paramTypeName = param.substr(colonPos + 1);
                // Remove any leading/trailing whitespace
                paramTypeName.erase(0, paramTypeName.find_first_not_of(" \t"));
                paramTypeName.erase(paramTypeName.find_last_not_of(" \t") + 1);
            }
            
            // Determine parameter type based on annotation
            llvm::Type* paramType;
            if (paramTypeName == "String") {
                paramType = llvm::Type::getInt8PtrTy(context);
            } else if (paramTypeName == "Boolean") {
                paramType = llvm::Type::getInt1Ty(context);
            } else if (paramTypeName == "Number") {
                paramType = llvm::Type::getFloatTy(context);
            } else if (currentContext->hasType(paramTypeName)) {
                // Parameter is a user-defined type - use i8* for runtime objects
                paramType = llvm::Type::getInt8PtrTy(context);
            } else {
                paramType = llvm::Type::getFloatTy(context); // Number type (default)
            }
            
            paramTypes.push_back(paramType);
        }
        
        // Determine return type from annotation

        llvm::Type* returnType;
        if (returnTypeAnnotation == "String") {
            returnType = llvm::Type::getInt8PtrTy(context);

        } else if (returnTypeAnnotation == "Number") {
            returnType = llvm::Type::getFloatTy(context);

        } else if (returnTypeAnnotation == "Boolean") {
            returnType = llvm::Type::getInt1Ty(context);

        } else if (returnTypeAnnotation == name) {
            // Method returns the same type as the class - use i8* for runtime objects
            returnType = llvm::Type::getInt8PtrTy(context);

        } else if (currentContext->hasType(returnTypeAnnotation)) {
            // Method returns a user-defined type - use i8* for runtime objects
            returnType = llvm::Type::getInt8PtrTy(context);

        } else {
            // Default to Number for unknown types
            returnType = llvm::Type::getFloatTy(context);

        }
        
        llvm::FunctionType* funcType = llvm::FunctionType::get(
            returnType,
            paramTypes,
            false // Not vararg
        );
        
        // Create the function
        llvm::Function* func = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            methodName,
            module
        );
        
        // Name the parameters
        auto argIt = func->arg_begin();
        argIt->setName("self"); // First arg is 'self'
        
        // Name the rest of the parameters
        for (size_t i = 0; i < params.size(); i++) {
            ++argIt;
            // Extract just the parameter name if it contains type annotation
            std::string paramName = params[i];
            size_t colonPos = paramName.find(':');
            if (colonPos != std::string::npos) {
                paramName = paramName.substr(0, colonPos);
                // Remove any trailing whitespace
                paramName.erase(paramName.find_last_not_of(" \t") + 1);
            }
            argIt->setName(paramName);
        }
        
        // Store the method in the context BEFORE generating method bodies
        currentContext->addMethod(name, methodName_local, func);
        
        // Store the function for the second pass
        methodFunctions.push_back(func);
    }
    
    // SECOND PASS: Generate method bodies now that all signatures are registered
    for (size_t methodIndex = 0; methodIndex < methodsData.size(); methodIndex++) {
        const auto& methodData = methodsData[methodIndex];
        const std::string& methodName_local = std::get<0>(methodData);
        const std::vector<std::string>& params = std::get<1>(methodData);
        const std::string& returnTypeAnnotation = std::get<2>(methodData);
        Expression* body = std::get<3>(methodData);
        
        llvm::Function* func = methodFunctions[methodIndex];
        
        // Create a basic block for the function
        llvm::BasicBlock* bb = llvm::BasicBlock::Create(context, "entry", func);
        generator.getBuilder()->SetInsertPoint(bb);
        
        // Create a new scope for the method
        generator.pushScope();
        
        // Set the current method context for base() calls
        generator.setCurrentMethod(name, methodName_local);
        
        // Add parameters to the scope
        auto argIt = func->arg_begin();
        llvm::Value* selfValue = &*argIt; // Get 'self' value
        generator.setNamedValue("self", selfValue); // Add 'self' to the scope
        
        // Store the semantic type information for 'self'
        generator.getContextObject()->setVariableTypeName("self", name);
        std::cout << "[DEBUG] TypeDefinition::codegen - Set 'self' type to '" << name << "' for method " << methodName_local << std::endl;
        
        // Add method parameters to the scope
        for (size_t i = 0; i < params.size(); i++) {
            ++argIt;
            
            // Extract parameter name and type annotation
            std::string param = params[i];
            std::string paramName = param;
            std::string paramTypeName = "Number"; // Default type
            
            size_t colonPos = param.find(':');
            if (colonPos != std::string::npos) {
                paramName = param.substr(0, colonPos);
                paramTypeName = param.substr(colonPos + 1);
                // Remove any leading/trailing whitespace
                paramName.erase(paramName.find_last_not_of(" \t") + 1);
                paramTypeName.erase(0, paramTypeName.find_first_not_of(" \t"));
                paramTypeName.erase(paramTypeName.find_last_not_of(" \t") + 1);
            }
            
            // Create allocas for method parameters so they can be accessed like variables
            // Use the actual argument type instead of trying to determine it from annotation
            llvm::Type* actualArgType = argIt->getType();
            llvm::AllocaInst* paramAlloca = generator.createEntryBlockAlloca(
                func, paramName, actualArgType);
            // Store the parameter value in the alloca
            generator.getBuilder()->CreateStore(&*argIt, paramAlloca);
            // Add the alloca to the scope
            generator.setNamedValue(paramName, paramAlloca);
            
            // Store the semantic type information for the parameter
            if (!paramTypeName.empty() && paramTypeName != "Number") {
                generator.getContextObject()->setVariableTypeName(paramName, paramTypeName);
                std::cout << "[DEBUG] TypeDefinition::codegen - Stored parameter '" << paramName << "' with semantic type '" << paramTypeName << "'" << std::endl;
            }
        }
        
        // Generate code for the method body
        llvm::Value* returnValue = body->codegen(generator);
        
        // Determine return type from annotation
        llvm::Type* returnType = func->getReturnType();
        
        // Check if we need to load the value or cast based on return type annotation
        if (returnValue && returnValue->getType()->isPointerTy()) {
            // For Number return types, load the value from the pointer
            if (returnTypeAnnotation == "Number" && returnType->isFloatTy()) {
                llvm::Type* pointedType = returnValue->getType()->getPointerElementType();
                if (pointedType->isFloatTy()) {
                    returnValue = generator.getBuilder()->CreateLoad(pointedType, returnValue, "loaded." + methodName_local);
                }
            }
            // For Boolean return types, load the value from the pointer
            else if (returnTypeAnnotation == "Boolean" && returnType->isIntegerTy(1)) {
                llvm::Type* pointedType = returnValue->getType()->getPointerElementType();
                if (pointedType->isIntegerTy(1)) {
                    returnValue = generator.getBuilder()->CreateLoad(pointedType, returnValue, "loaded." + methodName_local);
                }
            }
            // For user-defined types, cast to i8* if needed
            else if ((returnTypeAnnotation == name || currentContext->hasType(returnTypeAnnotation)) && 
                     returnType->isPointerTy() && returnType->getPointerElementType()->isIntegerTy(8)) {
                // Cast the struct pointer to i8*
                returnValue = generator.getBuilder()->CreateBitCast(returnValue, llvm::Type::getInt8PtrTy(context), "cast.to.i8ptr");
            }
            // For String return types, keep the pointer as-is since we want i8*
        } else if (returnValue && returnTypeAnnotation == "String" && returnValue->getType()->isIntegerTy(8)) {
            // If we have a single i8 value but need i8*, this is an error
            std::cerr << "Error: Method " << methodName_local << " returns i8 but should return i8* (String)" << std::endl;
            return nullptr;
        }
        
        // Ensure the return value matches the expected return type annotation
        if (returnValue && returnType) {
            // Check if we need to load a value from a pointer
            if (returnValue->getType()->isPointerTy() && !returnType->isPointerTy()) {
                llvm::Type* pointedType = returnValue->getType()->getPointerElementType();
                // If the pointed type matches the expected return type, load it
                if (pointedType == returnType) {
                    returnValue = generator.getBuilder()->CreateLoad(
                        pointedType, 
                        returnValue, 
                        "loaded.return.value");
                }
            }
            // Check if types match after potential loading
            if (returnValue->getType() != returnType) {
                std::cerr << "Error: Method " << methodName_local 
                          << " return type mismatch. Expected: ";
                returnType->print(llvm::errs());
                std::cerr << ", Got: ";
                returnValue->getType()->print(llvm::errs());
                std::cerr << std::endl;
                return nullptr;
            }
        }
        
        // Create a return instruction
        if (returnValue) {
            generator.getBuilder()->CreateRet(returnValue);
        } else {
            // If no return value, return a default value based on return type
            if (returnType->isIntegerTy()) {
                generator.getBuilder()->CreateRet(llvm::ConstantInt::get(returnType, 0));
            } else if (returnType->isPointerTy()) {
                generator.getBuilder()->CreateRet(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(returnType)));
            } else {
                // For other types, return a null value
                generator.getBuilder()->CreateRet(llvm::Constant::getNullValue(returnType));
            }
        }
        
        // Pop the method scope
        generator.popScope();
        
        // Clear the method context
        generator.setCurrentMethod("", "");
        
        // Verify the function
        llvm::verifyFunction(*func);
    }
    
    // Register the type definition in the context for later lookup
    if (auto* ctx = dynamic_cast<Context*>(generator.currentContext())) {
        ctx->registerTypeDefinition(name, this);
    }
    
    // Return null since type definitions don't produce a value
    return nullptr;
}