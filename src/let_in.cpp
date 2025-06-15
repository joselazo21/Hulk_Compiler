#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

// Helper function to find common base type between two types
std::string findCommonBaseType(const std::string& type1, const std::string& type2, IContext* context) {
    if (type1 == type2) {
        return type1;
    }
    
    // Check if type1 is a subtype of type2
    if (context->isSubtypeOf(type1, type2)) {
        return type2;
    }
    
    // Check if type2 is a subtype of type1
    if (context->isSubtypeOf(type2, type1)) {
        return type1;
    }
    
    // Find common ancestor by walking up the inheritance chain
    // Build the inheritance chain for type1
    std::vector<std::string> chain1;
    std::string current1 = type1;
    while (current1 != "Object" && !current1.empty()) {
        chain1.push_back(current1);
        current1 = context->getParentType(current1);
    }
    chain1.push_back("Object");
    
    // Walk up the inheritance chain for type2 and check if any type is in chain1
    std::string current2 = type2;
    while (current2 != "Object" && !current2.empty()) {
        // Check if current2 is in chain1
        for (const std::string& ancestor : chain1) {
            if (current2 == ancestor) {
                return current2;
            }
        }
        current2 = context->getParentType(current2);
    }
    
    // If we reach here, the common ancestor is Object
    return "Object";
}


LetIn::LetIn(const std::vector<std::pair<std::string, Expression*>>& decls, Expression* expr)
    : Expression(SourceLocation()), decls(decls), expr(expr), useTypedDecls(false) {}

LetIn::LetIn(const SourceLocation& loc, 
            const std::vector<std::pair<std::string, Expression*>>& decls, 
            Expression* expr)
    : Expression(loc), decls(decls), expr(expr), useTypedDecls(false) {}

// New constructor with type annotations
LetIn::LetIn(const SourceLocation& loc, const std::vector<LetBinding>& typedDecls, Expression* expr)
    : Expression(loc), typedDecls(typedDecls), expr(expr), useTypedDecls(true) {}

std::string LetIn::toString() {
    std::string result = "let ";
    if (useTypedDecls) {
        for (size_t i = 0; i < typedDecls.size(); i++) {
            if (i > 0) result += ", ";
            result += typedDecls[i].name;
            if (!typedDecls[i].typeAnnotation.empty()) {
                result += " : " + typedDecls[i].typeAnnotation;
            }
            result += " = " + typedDecls[i].expr->toString();
        }
    } else {
        for (size_t i = 0; i < decls.size(); i++) {
            if (i > 0) result += ", ";
            result += decls[i].first + " = " + decls[i].second->toString();
        }
    }
    result += " in ";
    if (expr) result += expr->toString();
    else result += "[null]";
    return result;
}

void LetIn::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── LetIn:\n";
    printIndent(depth);
    std::cout << "│   ├── Declarations:\n";
    if (useTypedDecls) {
        for (const auto& binding : typedDecls) {
            printIndent(depth + 2);
            std::cout << "├── " << binding.name;
            if (!binding.typeAnnotation.empty()) {
                std::cout << " : " << binding.typeAnnotation;
            }
            std::cout << " = \n";
            binding.expr->printNode(depth + 3);
        }
    } else {
        for (const auto& decl : decls) {
            printIndent(depth + 2);
            std::cout << "├── " << decl.first << " = \n";
            decl.second->printNode(depth + 3);
        }
    }
    printIndent(depth);
    std::cout << "│   └── In Expression:\n";
    if (expr) expr->printNode(depth + 2);
}

bool LetIn::Validate(IContext* context) {
    IContext* letContext = context->createChildContext();

    // Check which declaration format to use
    if (useTypedDecls) {
        // Process typed declarations with type checking
        for (const auto& binding : typedDecls) {
            // Check if trying to declare 'self' - this is not allowed
            if (binding.name == "self") {
                SEMANTIC_ERROR("Cannot declare variable 'self': 'self' is not a valid assignment target", location);
                delete letContext;
                return false;
            }
            
            llvm::LLVMContext* llvmCtx = context->getLLVMContext();
            
            // Infer the actual type of the declaration expression
            llvm::Type* actualType = nullptr;
            std::string actualTypeName;
            
            // Check if it's a number literal
            if (dynamic_cast<Number*>(binding.expr)) {
                actualType = llvm::Type::getFloatTy(*llvmCtx);
                actualTypeName = "Number";
                std::cout << "[DEBUG] LetIn::Validate - Variable " << binding.name << " is Number literal" << std::endl;
            }
            // Check if it's a string literal
            else if (dynamic_cast<StringLiteral*>(binding.expr)) {
                actualType = llvm::Type::getInt8PtrTy(*llvmCtx);
                actualTypeName = "String";
                std::cout << "[DEBUG] LetIn::Validate - Variable " << binding.name << " is String literal" << std::endl;
            }
            // Check if it's a new expression
            else if (auto* newExpr = dynamic_cast<NewExpression*>(binding.expr)) {
                std::string typeName = newExpr->getTypeName();
                // Check if the type name is registered in the context (even if struct is nullptr during validation)
                if (letContext->hasType(typeName)) {
                    // During validation, we may not have the actual struct type yet, but we know the type exists
                    actualType = llvm::Type::getFloatTy(*llvmCtx); // Placeholder type for validation
                    actualTypeName = typeName;
                    std::cout << "[DEBUG] LetIn::Validate - Variable " << binding.name << " is NewExpression of type " << typeName << std::endl;
                } else {
                    actualType = llvm::Type::getFloatTy(*llvmCtx);
                    actualTypeName = "Number";
                    std::cout << "[DEBUG] LetIn::Validate - Variable " << binding.name << " is NewExpression but type not found, defaulting to Number" << std::endl;
                }
            }
            // Check if it's an if expression
            else if (auto* ifExpr = dynamic_cast<IfExpression*>(binding.expr)) {
                // For if expressions, we need to infer the common type of both branches
                // For now, we'll check if both branches are new expressions of compatible types
                if (auto* thenNew = dynamic_cast<NewExpression*>(ifExpr->getThenExpr())) {
                    if (auto* elseNew = dynamic_cast<NewExpression*>(ifExpr->getElseExpr())) {
                        std::string thenType = thenNew->getTypeName();
                        std::string elseType = elseNew->getTypeName();
                        
                        // Find common base type
                        std::string commonType = findCommonBaseType(thenType, elseType, letContext);
                        
                        if (!commonType.empty() && commonType != "Object") {
                            // Check if the common type is registered (even if struct is nullptr during validation)
                            if (letContext->hasType(commonType)) {
                                // During validation, use placeholder type but keep the correct type name
                                actualType = llvm::Type::getFloatTy(*llvmCtx);
                                actualTypeName = commonType;
                            } else {
                                // Even if struct type is not found, we can still use the common type name
                                actualType = llvm::Type::getFloatTy(*llvmCtx);
                                actualTypeName = commonType;
                            }
                        } else {
                            actualType = llvm::Type::getFloatTy(*llvmCtx);
                            actualTypeName = "Number";
                        }
                    } else {
                        actualType = llvm::Type::getFloatTy(*llvmCtx);
                        actualTypeName = "Number";
                        std::cout << "[DEBUG] LetIn::Validate - Variable " << binding.name << " is IfExpression but else branch is not NewExpression, defaulting to Number" << std::endl;
                    }
                } else {
                    actualType = llvm::Type::getFloatTy(*llvmCtx);
                    actualTypeName = "Number";
                    std::cout << "[DEBUG] LetIn::Validate - Variable " << binding.name << " is IfExpression but then branch is not NewExpression, defaulting to Number" << std::endl;
                }
            }
            // Check if it's an as expression (type casting)
            else if (auto* asExpr = dynamic_cast<AsExpression*>(binding.expr)) {
                std::string targetTypeName = asExpr->getTypeName();
                // Check if the target type is registered (even if struct is nullptr during validation)
                if (letContext->hasType(targetTypeName)) {
                    // During validation, use placeholder type but keep the correct type name
                    actualType = llvm::Type::getFloatTy(*llvmCtx);
                    actualTypeName = targetTypeName;
                    std::cout << "[DEBUG] LetIn::Validate - Variable " << binding.name << " is AsExpression with target type " << targetTypeName << std::endl;
                } else {
                    // Even if struct type is not found, we can still use the target type name
                    actualType = llvm::Type::getFloatTy(*llvmCtx);
                    actualTypeName = targetTypeName;
                    std::cout << "[DEBUG] LetIn::Validate - Variable " << binding.name << " is AsExpression but target type not found, using type name " << targetTypeName << std::endl;
                }
            }
            // Default to Number for other expressions
            else {
                actualType = llvm::Type::getFloatTy(*llvmCtx);
                actualTypeName = "Number";
                std::cout << "[DEBUG] LetIn::Validate - Variable " << binding.name << " is other expression, defaulting to Number type" << std::endl;
            }
            
            // Check type annotation if present
            if (!binding.typeAnnotation.empty()) {
                std::cout << "[DEBUG] LetIn::Validate - Checking type annotation: " << binding.typeAnnotation << " vs actual: " << actualTypeName << std::endl;
                
                // Check for type compatibility (exact match or inheritance)
                bool isCompatible = false;
                if (binding.typeAnnotation == actualTypeName) {
                    // Exact type match
                    isCompatible = true;
                    std::cout << "[DEBUG] LetIn::Validate - Exact type match" << std::endl;
                } else if (letContext->isSubtypeOf(actualTypeName, binding.typeAnnotation)) {
                    // actualTypeName is a subtype of the declared type (inheritance)
                    isCompatible = true;
                    std::cout << "[DEBUG] LetIn::Validate - " << actualTypeName << " is a subtype of " << binding.typeAnnotation << std::endl;
                } else {
                    std::cout << "[DEBUG] LetIn::Validate - Types are not compatible" << std::endl;
                }
                
                if (!isCompatible) {
                    SEMANTIC_ERROR("Type mismatch: variable '" + binding.name + "' declared as " + 
                                 binding.typeAnnotation + " but assigned " + actualTypeName, location);
                    delete letContext;
                    return false;
                }
                std::cout << "[DEBUG] LetIn::Validate - Type annotation is compatible with actual type" << std::endl;
            }
            
            if (!letContext->addVariable(binding.name, actualType, actualTypeName)) {
                SEMANTIC_ERROR("Variable " + binding.name + " already defined in this scope", location);
                delete letContext;
                return false;
            }
            
            std::cout << "[DEBUG] LetIn::Validate - Added variable " << binding.name << " to context with type " << actualTypeName << std::endl;
        }

        // Now validate each declaration expression using the extended context
        for (const auto& binding : typedDecls) {
            if (!binding.expr->Validate(letContext)) {
                SEMANTIC_ERROR("Error in declaration of " + binding.name, location);
                delete letContext;
                return false;
            }
        }
    } else {
        // Legacy processing for backward compatibility
        for (const auto& decl : decls) {
            // Check if trying to declare 'self' - this is not allowed
            if (decl.first == "self") {
                SEMANTIC_ERROR("Cannot declare variable 'self': 'self' is not a valid assignment target", location);
                delete letContext;
                return false;
            }
            
            llvm::LLVMContext* llvmCtx = context->getLLVMContext();
            
            // Infer the type of the declaration expression
            llvm::Type* varType = nullptr;
            
            // Check if it's a number literal
            if (dynamic_cast<Number*>(decl.second)) {
                varType = llvm::Type::getFloatTy(*llvmCtx); // Number type
                std::cout << "[DEBUG] LetIn::Validate - Variable " << decl.first << " is Number literal, setting Float type" << std::endl;
            }
            // Check if it's a string literal
            else if (dynamic_cast<StringLiteral*>(decl.second)) {
                varType = llvm::Type::getInt8PtrTy(*llvmCtx); // String type
                std::cout << "[DEBUG] LetIn::Validate - Variable " << decl.first << " is String literal, setting Pointer type" << std::endl;
            }
            // Default to Number for other expressions
            else {
                varType = llvm::Type::getFloatTy(*llvmCtx);
                std::cout << "[DEBUG] LetIn::Validate - Variable " << decl.first << " is other expression, defaulting to Float type" << std::endl;
            }
            
            if (!letContext->addVariable(decl.first, varType)) {
                SEMANTIC_ERROR("Variable " + decl.first + " already defined in this scope", location);
                delete letContext;
                return false;
            }
            
            std::cout << "[DEBUG] LetIn::Validate - Added variable " << decl.first << " to context with type" << std::endl;
        }

        // Now validate each declaration expression using the extended context
        for (const auto& decl : decls) {
            if (!decl.second->Validate(letContext)) {
                SEMANTIC_ERROR("Error in declaration of " + decl.first, location);
                delete letContext;
                return false;
            }
        }
    }
    
    // Validate the 'in' expression using the extended context
    std::cout << "[DEBUG] LetIn::Validate - About to validate 'in' expression with letContext: " << letContext << std::endl;
    if (expr && !expr->Validate(letContext)) {
        SEMANTIC_ERROR("Error in 'in' expression of let statement", location);
        delete letContext;
        return false;
    }
    
    delete letContext;
    return true;
}

LetIn::~LetIn() {
    if (useTypedDecls) {
        for (auto& binding : typedDecls) {
            delete binding.expr;
        }
    } else {
        for (auto& decl : decls) {
            delete decl.second;
        }
    }
    delete expr;
}


llvm::Value* LetIn::codegen(CodeGenerator& generator) {
    IContext* oldContext = generator.getContextObject();
    IContext* letContext = oldContext ? oldContext->createChildContext() : nullptr;
    generator.setContextObject(letContext);

    // Declarar oldBindings para guardar los valores antiguos de las variables
    std::map<std::string, llvm::Value*> oldBindings;
    
    // Mantener un registro de las variables declaradas en este let-in
    std::vector<std::string> declaredVars;

    // Procesar declaraciones y guardar valores antiguos
    if (useTypedDecls) {
        for (const auto& binding : typedDecls) {
            const std::string& varName = binding.name;
            declaredVars.push_back(varName);
            
            llvm::Value* oldVal = generator.getNamedValue(varName);
            if (oldVal) {
                oldBindings[varName] = oldVal;
            }

            // Set the current variable name context for vector data association
            std::cout << "[DEBUG] LetIn::codegen - setting current variable name to: '" << varName << "'" << std::endl;
            generator.setCurrentLetInVariableName(varName);
            
            // Generar valor de la expresión de la declaración
            llvm::Value* val = binding.expr->codegen(generator);
        
        // Clear the current variable name context
        std::cout << "[DEBUG] LetIn::codegen - clearing current variable name" << std::endl;
        generator.setCurrentLetInVariableName("");
        if (!val) {
        std::cerr << "Warning: Failed to generate code for declaration of " << varName << ", using default value" << std::endl;
        val = llvm::ConstantFP::get(llvm::Type::getFloatTy(generator.getContext()), 0.0f);
        }

        // Detectar si es un iterador
        bool isIter = varName.rfind("__iter_", 0) == 0;

        if (isIter) {
            // Para iteradores, usar la variable global directamente
            // 1. Get or create the global variable
            llvm::GlobalVariable* globalIter = generator.getIteratorGlobalVariable(varName);
            
            // 2. Store the range value in the global - ensure type compatibility
            llvm::Type* globalType = globalIter->getValueType();
            if (val->getType() != globalType) {
                // If types don't match, we need to handle the conversion
                if (globalType->isPointerTy() && val->getType()->isPointerTy()) {
                    // Both are pointers, try bitcast
                    val = generator.getBuilder()->CreateBitCast(val, globalType, varName + "_cast");
                } else if (globalType->isPointerTy() && val->getType()->isIntegerTy(32)) {
                    // If global expects a pointer but we have an integer, this might be a variable reference
                    // Try to load the actual range pointer from the variable
                    if (auto* varExpr = dynamic_cast<Variable*>(binding.expr)) {
                        std::string sourceVarName = varExpr->getName();
                        llvm::Value* sourceVar = generator.getNamedValue(sourceVarName);
                        if (sourceVar && llvm::isa<llvm::AllocaInst>(sourceVar)) {
                            llvm::AllocaInst* alloca = llvm::cast<llvm::AllocaInst>(sourceVar);
                            if (alloca->getAllocatedType()->isPointerTy()) {
                                val = generator.getBuilder()->CreateLoad(alloca->getAllocatedType(), alloca, sourceVarName + "_load");
                                if (val->getType() != globalType) {
                                    val = generator.getBuilder()->CreateBitCast(val, globalType, varName + "_cast");
                                }
                            }
                        }
                    }
                    
                    // If still not compatible, show debug info and error
                    if (val->getType() != globalType) {
                        std::cout << "[DEBUG] Type mismatch for " << varName << std::endl;
                        std::cout << "[DEBUG] Expected type: ";
                        globalType->print(llvm::errs());
                        std::cout << std::endl;
                        std::cout << "[DEBUG] Actual type: ";
                        val->getType()->print(llvm::errs());
                        std::cout << std::endl;
                        
                        SEMANTIC_ERROR("Type mismatch when storing iterator value for '" + varName + "'", getLocation());
                        generator.setContextObject(oldContext);
                        if (letContext) delete letContext;
                        return nullptr;
                    }
                } else {
                    // Debug information to understand the type mismatch
                    std::cout << "[DEBUG] Type mismatch for " << varName << std::endl;
                    std::cout << "[DEBUG] Expected type: ";
                    globalType->print(llvm::errs());
                    std::cout << std::endl;
                    std::cout << "[DEBUG] Actual type: ";
                    val->getType()->print(llvm::errs());
                    std::cout << std::endl;
                    
                    SEMANTIC_ERROR("Type mismatch when storing iterator value for '" + varName + "'", getLocation());
                    generator.setContextObject(oldContext);
                    if (letContext) delete letContext;
                    return nullptr;
                }
            }
            generator.getBuilder()->CreateStore(val, globalIter);
            
            // 3. Check if this is a vector expression and transfer vector data
            std::cout << "[DEBUG] Checking if declaration is a vector expression for " << varName << std::endl;
            if (auto* vectorExpr = dynamic_cast<VectorExpression*>(binding.expr)) {
                std::cout << "[DEBUG] Found vector expression, isGenerator: " << vectorExpr->getIsGenerator() << std::endl;
                if (!vectorExpr->getIsGenerator()) {
                    // Extract vector values and store them for the iterator
                    std::vector<int> vectorValues;
                    std::cout << "[DEBUG] Extracting " << vectorExpr->getElements().size() << " elements" << std::endl;
                    for (auto* elem : vectorExpr->getElements()) {
                        if (auto* numberExpr = dynamic_cast<Number*>(elem)) {
                            int value = static_cast<int>(numberExpr->getValue());
                            std::cout << "[DEBUG] Found number element: " << value << std::endl;
                            vectorValues.push_back(value);
                        } else {
                            std::cout << "[DEBUG] Element is not a Number" << std::endl;
                        }
                    }
                    if (!vectorValues.empty()) {
                        std::cout << "[DEBUG] Storing vector data for " << varName << std::endl;
                        generator.storeVectorDataForIterator(varName, vectorValues);
                    } else {
                        std::cout << "[DEBUG] No vector values to store" << std::endl;
                    }
                }
            } else if (auto* varExpr = dynamic_cast<Variable*>(binding.expr)) {
                // Check if this iterator is being assigned a variable that has vector data
                std::string sourceVarName = varExpr->getName();
                std::cout << "[DEBUG] Iterator " << varName << " is being assigned variable " << sourceVarName << std::endl;
                
                // Try to find vector data for the source variable
                if (generator.hasVectorDataForIterator(sourceVarName)) {
                    std::vector<int> sourceVectorData = generator.getVectorDataForIterator(sourceVarName);
                    std::cout << "[DEBUG] Found vector data for source variable " << sourceVarName << ", transferring to " << varName << std::endl;
                    std::cout << "[DEBUG] Vector data values: ";
                    for (int val : sourceVectorData) {
                        std::cout << val << " ";
                    }
                    std::cout << std::endl;
                    generator.storeVectorDataForIterator(varName, sourceVectorData);
                } else {
                    std::cout << "[DEBUG] No vector data found for source variable " << sourceVarName << std::endl;
                    
                    // Try to find the actual variable and check if it has vector data stored
                    llvm::Value* sourceVar = generator.getNamedValue(sourceVarName);
                    if (sourceVar) {
                        std::cout << "[DEBUG] Found source variable " << sourceVarName << ", checking for associated vector data" << std::endl;
                        
                        // Check if there's vector data stored for any key that might match
                        // Look for vector data that was stored when the variable was created
                        auto vectorDataMap = generator.getAllVectorData();
                        for (const auto& pair : vectorDataMap) {
                            std::cout << "[DEBUG] Checking vector data key: " << pair.first << std::endl;
                            if (pair.first.find(sourceVarName) != std::string::npos || 
                                pair.first == sourceVarName) {
                                std::cout << "[DEBUG] Found matching vector data for key " << pair.first << ", transferring to " << varName << std::endl;
                                std::cout << "[DEBUG] Vector data values: ";
                                for (int val : pair.second) {
                                    std::cout << val << " ";
                                }
                                std::cout << std::endl;
                                generator.storeVectorDataForIterator(varName, pair.second);
                                break;
                            }
                        }
                    }
                }
            } else {
                std::cout << "[DEBUG] Declaration is not a vector expression or variable" << std::endl;
            }
            
            // 4. NOW implement the iterator functions (after the global is created and value stored)
            generator.implementIteratorFunctions(varName);
            
            // 5. Store the global pointer as the named value for this iterator
            generator.setNamedValue(varName, globalIter);
        } else {
            // Para variables normales, crear alloca local
            // For object types, preserve the runtime type information by storing as i8* (generic pointer)
            // but keep track of the actual runtime type
            llvm::Type* varType = val->getType(); // Use the type of the value
            std::string semanticTypeName; // Track the semantic type name
            
            // If this is a runtime object type (pointer to struct with runtime type info),
            // store it as i8* to preserve polymorphism while keeping runtime type info
            if (varType->isPointerTy()) {
                llvm::Type* pointedType = varType->getPointerElementType();
                if (pointedType->isStructTy()) {
                    llvm::StructType* structType = llvm::cast<llvm::StructType>(pointedType);
                    std::string typeName = structType->getName().str();
                    if (typeName.find("runtime.") == 0) {
                        // Extract the semantic type name (remove "runtime." prefix)
                        semanticTypeName = typeName.substr(8);
                        // This is a runtime object, store as i8* to preserve polymorphism
                        varType = llvm::Type::getInt8PtrTy(generator.getContext());
                        // Cast the value to i8* for storage
                        val = generator.getBuilder()->CreateBitCast(val, varType, varName + "_cast");
                    } else if (typeName.find("struct.") == 0) {
                        // Extract the semantic type name (remove "struct." prefix)
                        semanticTypeName = typeName.substr(7);
                    } else {
                        semanticTypeName = typeName;
                    }
                }
            }

            llvm::Function* func = generator.getBuilder()->GetInsertBlock()->getParent();
            llvm::IRBuilder<> tmpBuilder(&func->getEntryBlock(), func->getEntryBlock().begin());
            llvm::AllocaInst* alloca = tmpBuilder.CreateAlloca(varType, nullptr, varName);

            generator.getBuilder()->CreateStore(val, alloca);
            generator.setNamedValue(varName, alloca);
            
            // Store the semantic type name if we have one
            if (!semanticTypeName.empty()) {
                generator.getContextObject()->setVariableTypeName(varName, semanticTypeName);
                std::cout << "[DEBUG] LetIn::codegen - Stored semantic type name '" << semanticTypeName << "' for variable '" << varName << "'" << std::endl;
            }
        }
    }
    } else {
        // Legacy processing for backward compatibility
        for (const auto& decl : decls) {
            const std::string& varName = decl.first;
            declaredVars.push_back(varName);
            
            llvm::Value* oldVal = generator.getNamedValue(varName);
            if (oldVal) {
                oldBindings[varName] = oldVal;
            }

            // Set the current variable name context for vector data association
            std::cout << "[DEBUG] LetIn::codegen - setting current variable name to: '" << varName << "'" << std::endl;
            generator.setCurrentLetInVariableName(varName);
            
            // Generar valor de la expresión de la declaración
            llvm::Value* val = decl.second->codegen(generator);
            
            // Clear the current variable name context
            std::cout << "[DEBUG] LetIn::codegen - clearing current variable name" << std::endl;
            generator.setCurrentLetInVariableName("");
            if (!val) {
                std::cerr << "Warning: Failed to generate code for declaration of " << varName << ", using default value" << std::endl;
                val = llvm::ConstantFP::get(llvm::Type::getFloatTy(generator.getContext()), 0.0f);
            }

            // Detectar si es un iterador
            bool isIter = varName.rfind("__iter_", 0) == 0;

            if (isIter) {
                // Para iteradores, usar la variable global directamente
                // 1. Get or create the global variable
                llvm::GlobalVariable* globalIter = generator.getIteratorGlobalVariable(varName);
                
                // 2. Store the range value in the global - ensure type compatibility
                llvm::Type* globalType = globalIter->getValueType();
                if (val->getType() != globalType) {
                    // If types don't match, we need to handle the conversion
                    if (globalType->isPointerTy() && val->getType()->isPointerTy()) {
                        // Both are pointers, try bitcast
                        val = generator.getBuilder()->CreateBitCast(val, globalType, varName + "_cast");
                    } else if (globalType->isPointerTy() && val->getType()->isIntegerTy(32)) {
                        // If global expects a pointer but we have an integer, this might be a variable reference
                        // Try to load the actual range pointer from the variable
                        if (auto* varExpr = dynamic_cast<Variable*>(decl.second)) {
                            std::string sourceVarName = varExpr->getName();
                            llvm::Value* sourceVar = generator.getNamedValue(sourceVarName);
                            if (sourceVar && llvm::isa<llvm::AllocaInst>(sourceVar)) {
                                llvm::AllocaInst* alloca = llvm::cast<llvm::AllocaInst>(sourceVar);
                                if (alloca->getAllocatedType()->isPointerTy()) {
                                    val = generator.getBuilder()->CreateLoad(alloca->getAllocatedType(), alloca, sourceVarName + "_load");
                                    if (val->getType() != globalType) {
                                        val = generator.getBuilder()->CreateBitCast(val, globalType, varName + "_cast");
                                    }
                                }
                            }
                        }
                        
                        // If still not compatible, show debug info and error
                        if (val->getType() != globalType) {
                            std::cout << "[DEBUG] Type mismatch for " << varName << std::endl;
                            std::cout << "[DEBUG] Expected type: ";
                            globalType->print(llvm::errs());
                            std::cout << std::endl;
                            std::cout << "[DEBUG] Actual type: ";
                            val->getType()->print(llvm::errs());
                            std::cout << std::endl;
                            
                            SEMANTIC_ERROR("Type mismatch when storing iterator value for '" + varName + "'", getLocation());
                            generator.setContextObject(oldContext);
                            if (letContext) delete letContext;
                            return nullptr;
                        }
                    } else {
                        // Debug information to understand the type mismatch
                        std::cout << "[DEBUG] Type mismatch for " << varName << std::endl;
                        std::cout << "[DEBUG] Expected type: ";
                        globalType->print(llvm::errs());
                        std::cout << std::endl;
                        std::cout << "[DEBUG] Actual type: ";
                        val->getType()->print(llvm::errs());
                        std::cout << std::endl;
                        
                        SEMANTIC_ERROR("Type mismatch when storing iterator value for '" + varName + "'", getLocation());
                        generator.setContextObject(oldContext);
                        if (letContext) delete letContext;
                        return nullptr;
                    }
                }
                generator.getBuilder()->CreateStore(val, globalIter);
                
                // 3. Check if this is a vector expression and transfer vector data
                std::cout << "[DEBUG] Checking if declaration is a vector expression for " << varName << std::endl;
                if (auto* vectorExpr = dynamic_cast<VectorExpression*>(decl.second)) {
                    std::cout << "[DEBUG] Found vector expression, isGenerator: " << vectorExpr->getIsGenerator() << std::endl;
                    if (!vectorExpr->getIsGenerator()) {
                        // Extract vector values and store them for the iterator
                        std::vector<int> vectorValues;
                        std::cout << "[DEBUG] Extracting " << vectorExpr->getElements().size() << " elements" << std::endl;
                        for (auto* elem : vectorExpr->getElements()) {
                            if (auto* numberExpr = dynamic_cast<Number*>(elem)) {
                                int value = static_cast<int>(numberExpr->getValue());
                                std::cout << "[DEBUG] Found number element: " << value << std::endl;
                                vectorValues.push_back(value);
                            } else {
                                std::cout << "[DEBUG] Element is not a Number" << std::endl;
                            }
                        }
                        if (!vectorValues.empty()) {
                            std::cout << "[DEBUG] Storing vector data for " << varName << std::endl;
                            generator.storeVectorDataForIterator(varName, vectorValues);
                        } else {
                            std::cout << "[DEBUG] No vector values to store" << std::endl;
                        }
                    }
                } else if (auto* varExpr = dynamic_cast<Variable*>(decl.second)) {
                    // Check if this iterator is being assigned a variable that has vector data
                    std::string sourceVarName = varExpr->getName();
                    std::cout << "[DEBUG] Iterator " << varName << " is being assigned variable " << sourceVarName << std::endl;
                    
                    // Try to find vector data for the source variable
                    if (generator.hasVectorDataForIterator(sourceVarName)) {
                        std::vector<int> sourceVectorData = generator.getVectorDataForIterator(sourceVarName);
                        std::cout << "[DEBUG] Found vector data for source variable " << sourceVarName << ", transferring to " << varName << std::endl;
                        std::cout << "[DEBUG] Vector data values: ";
                        for (int val : sourceVectorData) {
                            std::cout << val << " ";
                        }
                        std::cout << std::endl;
                        generator.storeVectorDataForIterator(varName, sourceVectorData);
                    } else {
                        std::cout << "[DEBUG] No vector data found for source variable " << sourceVarName << std::endl;
                        
                        // Try to find the actual variable and check if it has vector data stored
                        llvm::Value* sourceVar = generator.getNamedValue(sourceVarName);
                        if (sourceVar) {
                            std::cout << "[DEBUG] Found source variable " << sourceVarName << ", checking for associated vector data" << std::endl;
                            
                            // Check if there's vector data stored for any key that might match
                            // Look for vector data that was stored when the variable was created
                            auto vectorDataMap = generator.getAllVectorData();
                            for (const auto& pair : vectorDataMap) {
                                std::cout << "[DEBUG] Checking vector data key: " << pair.first << std::endl;
                                if (pair.first.find(sourceVarName) != std::string::npos || 
                                    pair.first == sourceVarName) {
                                    std::cout << "[DEBUG] Found matching vector data for key " << pair.first << ", transferring to " << varName << std::endl;
                                    std::cout << "[DEBUG] Vector data values: ";
                                    for (int val : pair.second) {
                                        std::cout << val << " ";
                                    }
                                    std::cout << std::endl;
                                    generator.storeVectorDataForIterator(varName, pair.second);
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    std::cout << "[DEBUG] Declaration is not a vector expression or variable" << std::endl;
                }
                
                // 4. NOW implement the iterator functions (after the global is created and value stored)
                generator.implementIteratorFunctions(varName);
                
                // 5. Store the global pointer as the named value for this iterator
                generator.setNamedValue(varName, globalIter);
            } else {
                // Para variables normales, crear alloca local
                // For object types, preserve the runtime type information by storing as i8* (generic pointer)
                // but keep track of the actual runtime type
                llvm::Type* varType = val->getType(); // Use the type of the value
                std::string semanticTypeName; // Track the semantic type name
                
                // If this is a runtime object type (pointer to struct with runtime type info),
                // store it as i8* to preserve polymorphism while keeping runtime type info
                if (varType->isPointerTy()) {
                    llvm::Type* pointedType = varType->getPointerElementType();
                    if (pointedType->isStructTy()) {
                        llvm::StructType* structType = llvm::cast<llvm::StructType>(pointedType);
                        std::string typeName = structType->getName().str();
                        if (typeName.find("runtime.") == 0) {
                            // Extract the semantic type name (remove "runtime." prefix)
                            semanticTypeName = typeName.substr(8);
                            // This is a runtime object, store as i8* to preserve polymorphism
                            varType = llvm::Type::getInt8PtrTy(generator.getContext());
                            // Cast the value to i8* for storage
                            val = generator.getBuilder()->CreateBitCast(val, varType, varName + "_cast");
                        } else if (typeName.find("struct.") == 0) {
                            // Extract the semantic type name (remove "struct." prefix)
                            semanticTypeName = typeName.substr(7);
                        } else {
                            semanticTypeName = typeName;
                        }
                    }
                }

                llvm::Function* func = generator.getBuilder()->GetInsertBlock()->getParent();
                llvm::IRBuilder<> tmpBuilder(&func->getEntryBlock(), func->getEntryBlock().begin());
                llvm::AllocaInst* alloca = tmpBuilder.CreateAlloca(varType, nullptr, varName);

                generator.getBuilder()->CreateStore(val, alloca);
                generator.setNamedValue(varName, alloca);
                
                // Store the semantic type name if we have one
                if (!semanticTypeName.empty()) {
                    generator.getContextObject()->setVariableTypeName(varName, semanticTypeName);
                    std::cout << "[DEBUG] LetIn::codegen - Stored semantic type name '" << semanticTypeName << "' for variable '" << varName << "'" << std::endl;
                }
            }
        }
    }

    llvm::Value* bodyVal = nullptr;
    if (expr) {
        bodyVal = expr->codegen(generator);
    }
    
    // Si el cuerpo no devolvió un valor significativo o es un bucle for/while
    // intentamos devolver el valor de una de las variables declaradas
    if (!bodyVal || bodyVal->getType()->isVoidTy() || 
        llvm::isa<llvm::Constant>(bodyVal)) {
        
        // Primero, buscar variables que parezcan acumuladores (no iteradores ni resultados temporales)
        for (const auto& varName : declaredVars) {
            // Evitar iteradores y variables temporales
            if (varName.rfind("__iter_", 0) == 0) continue;
            if (varName.rfind("__result_", 0) == 0) continue;
            
            // Nombres comunes para acumuladores
            if (varName == "result" || varName == "sum" || varName == "acc" || 
                varName == "factorial" || varName == "f" || varName == "total") {
                llvm::Value* var = generator.getNamedValue(varName);
                if (var && llvm::isa<llvm::AllocaInst>(var)) {
                    llvm::AllocaInst* alloca = llvm::cast<llvm::AllocaInst>(var);
                    bodyVal = generator.getBuilder()->CreateLoad(
                        alloca->getAllocatedType(),
                        alloca,
                        varName + ".final"
                    );
                    std::cout << "[LOG] LetIn::codegen, returning accumulator variable: " << varName << "\n";
                    break;
                }
            }
        }
        
        // Si no encontramos un acumulador por nombre, usar la primera variable no iterador
        if (!bodyVal) {
            for (const auto& varName : declaredVars) {
                // Evitar iteradores
                if (varName.rfind("__iter_", 0) == 0) continue;
                if (varName.rfind("__result_", 0) == 0) continue;
                
                llvm::Value* var = generator.getNamedValue(varName);
                if (var && llvm::isa<llvm::AllocaInst>(var)) {
                    llvm::AllocaInst* alloca = llvm::cast<llvm::AllocaInst>(var);
                    bodyVal = generator.getBuilder()->CreateLoad(
                        alloca->getAllocatedType(),
                        alloca,
                        varName + ".final"
                    );
                    std::cout << "[LOG] LetIn::codegen, returning first non-iterator variable: " << varName << "\n";
                    break;
                }
            }
        }
    }

    // Restore old bindings based on declaration type
    if (useTypedDecls) {
        for (const auto& binding : typedDecls) {
            const std::string& varName = binding.name;
            if (oldBindings.find(varName) != oldBindings.end()) {
                generator.setNamedValue(varName, oldBindings[varName]);
            } else {
                generator.removeNamedValue(varName);
            }
        }
    } else {
        for (const auto& decl : decls) {
            const std::string& varName = decl.first;
            if (oldBindings.find(varName) != oldBindings.end()) {
                generator.setNamedValue(varName, oldBindings[varName]);
            } else {
                generator.removeNamedValue(varName);
            }
        }
    }

    generator.setContextObject(oldContext);
    if (letContext) delete letContext;

    // Si aún no tenemos un valor, devolver un valor por defecto
    if (!bodyVal) {
        bodyVal = llvm::ConstantFP::get(llvm::Type::getFloatTy(generator.getContext()), 0.0f);
        std::cout << "[LOG] LetIn::codegen, no suitable return value found, returning default 0.0\n";
    }

    return bodyVal;
}
