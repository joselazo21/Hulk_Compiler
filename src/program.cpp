#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>
#include <unordered_set>
#include <unordered_map>
#include <functional>

Program::Program(const std::vector<FunctionDeclaration*>& funcs, const std::vector<Statement*>& stmts)
    : Node(SourceLocation()), functions(funcs), statements(stmts), types() {}

Program::Program(const SourceLocation& loc, const std::vector<FunctionDeclaration*>& funcs, const std::vector<Statement*>& stmts)
    : Node(loc), functions(funcs), statements(stmts), types() {}

Program::Program(const SourceLocation& loc, const std::vector<TypeDefinition*>& types, 
                 const std::vector<FunctionDeclaration*>& funcs, const std::vector<Statement*>& stmts)
    : Node(loc), functions(funcs), statements(stmts), types(types) {}

Program::~Program() {
    for (auto* t : types) delete t;
    for (auto* f : functions) delete f;
    for (auto* s : statements) delete s;
}

void Program::printNode(int depth) {
    printIndent(depth);
    std::cout << "├── Program\n";
    printIndent(depth + 1);
    std::cout << "├── Types:\n";
    for (auto* t : types) t->printNode(depth + 2);
    printIndent(depth + 1);
    std::cout << "├── Functions:\n";
    for (auto* f : functions) f->printNode(depth + 2);
    printIndent(depth + 1);
    std::cout << "├── Statements:\n";
    for (auto* s : statements) s->printNode(depth + 2);
}

bool Program::Validate(IContext* context) {
    bool hasErrors = false;
    
    // First pass: check for duplicate type definitions and register all type names in the context
    // This allows types to reference each other (including inheritance)
    std::unordered_set<std::string> typeNames;
    for (auto* t : types) {
        const std::string& typeName = t->getName();
        
        // Check for duplicate type definitions
        if (typeNames.find(typeName) != typeNames.end()) {
            SEMANTIC_ERROR("Type '" + typeName + "' is already defined", t->getLocation());
            hasErrors = true;
            continue; // Continue checking other types
        }
        
        // Check if trying to redefine builtin types
        if (typeName == "Number" || typeName == "String" || typeName == "Boolean" || typeName == "Object") {
            SEMANTIC_ERROR("Cannot redefine builtin type '" + typeName + "'", t->getLocation());
            hasErrors = true;
            continue; // Continue checking other types
        }
        
        typeNames.insert(typeName);
        
        // For now, just register the type name with a nullptr
        // The actual struct type will be created during codegen
        context->addType(typeName, nullptr);
    }
    
    // Second pass: register inheritance relationships
    for (auto* t : types) {
        if (t->getParentType() != "Object") {
            context->setParentType(t->getName(), t->getParentType());
        }
    }
    
    // Third pass: check for circular inheritance
    if (!checkCircularInheritance(context)) {
        hasErrors = true;
    }
    
    // Fourth pass: register type definitions in context for method lookup
    for (auto* t : types) {
        if (auto* ctx = dynamic_cast<Context*>(context)) {
            ctx->registerTypeDefinition(t->getName(), t);
        }
    }
    
    // Fifth pass: validate type definitions
    for (auto* t : types) {
        if (!t->Validate(context)) {
            hasErrors = true;
        }
    }
    
    // Sixth pass: register all functions first (before validating their bodies or statements)
    std::cout << "[DEBUG] Program::Validate - Registering " << functions.size() << " functions from functions vector" << std::endl;
    for (auto* f : functions) {
        // Register function signature without validating body yet
        std::cout << "[DEBUG] Registering function '" << f->getName() << "' with params: [";
        for (size_t i = 0; i < f->getParamTypes().size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << f->getParams()[i] << ":" << f->getParamTypes()[i];
        }
        std::cout << "] -> " << f->getReturnType() << std::endl;
        
        if (!context->addFunction(f->getName(), f->getParams(), f->getParamTypes(), f->getReturnType())) {
            SEMANTIC_ERROR("Function '" + f->getName() + "' already defined", f->getLocation());
            hasErrors = true;
        }
    }
    
    // Also register functions that might be in statements
    std::cout << "[DEBUG] Program::Validate - Checking " << statements.size() << " statements for function declarations" << std::endl;
    for (auto* s : statements) {
        if (auto* func_decl = dynamic_cast<FunctionDeclaration*>(s)) {
            std::cout << "[DEBUG] Found function declaration in statements: '" << func_decl->getName() << "' with params: [";
            for (size_t i = 0; i < func_decl->getParamTypes().size(); i++) {
                if (i > 0) std::cout << ", ";
                std::cout << func_decl->getParams()[i] << ":" << func_decl->getParamTypes()[i];
            }
            std::cout << "] -> " << func_decl->getReturnType() << std::endl;
            
            if (!context->addFunction(func_decl->getName(), func_decl->getParams(), func_decl->getParamTypes(), func_decl->getReturnType())) {
                SEMANTIC_ERROR("Function '" + func_decl->getName() + "' already defined", func_decl->getLocation());
                hasErrors = true;
            }
        }
    }
    
    // Seventh pass: validate function bodies
    for (auto* f : functions) {
        if (!f->Validate(context)) {
            hasErrors = true;
        }
    }
    
    // Eighth pass: validate statements (now functions are already registered)
    for (auto* s : statements) {
        if (!s->Validate(context)) {
            hasErrors = true;
        }
    }
    
    return !hasErrors;
}

llvm::Value* Program::codegen(CodeGenerator& generator) {
    std::cout << "Starting Program codegen...\n";
    
    std::cout << "Generating " << types.size() << " types...\n";
    for (auto* t : types) {
        std::cout << "Generating type...\n";
        t->codegen(generator);
    }
    
    std::cout << "Generating " << functions.size() << " functions...\n";
    for (auto* f : functions) {
        std::cout << "Generating function...\n";
        f->codegen(generator);
    }

    std::cout << "Generating " << statements.size() << " top-level statements...\n";
    if (!statements.empty()) {
        BlockStatement mainBlock(SourceLocation(), statements);
        llvm::Value* result = mainBlock.codegen(generator);
        std::cout << "Main block generation completed\n";
        if (result) {
            std::cout << "Main block returned value of type: ";
            result->getType()->print(llvm::errs());
            std::cout << "\n";
        } else {
            std::cout << "Main block returned nullptr\n";
        }
        return result;
    }
    
    std::cout << "Program codegen completed\n";
    return nullptr;
}


void Program::generateDeclarations(CodeGenerator& cg) {
    // Phase 1: Create empty struct types for all types
    // This allows types to reference each other during field resolution
    llvm::LLVMContext& context = cg.getContext();
    IContext* currentContext = cg.getContextObject();
    
    for (auto* t : types) {
        std::string structName = "struct." + t->getName();
        llvm::StructType* structType = llvm::StructType::create(context, structName);

        currentContext->addType(t->getName(), structType);
        
        // Set parent type relationship
        if (t->getParentType() != "Object") {
            currentContext->setParentType(t->getName(), t->getParentType());
        }
    }
    
    // Phase 2: Sort types by dependency order (parents before children)
    std::vector<TypeDefinition*> sortedTypes = topologicalSortTypes();
    
    // Phase 3: Now generate the complete type definitions with fields and methods in dependency order
    for (auto* t : sortedTypes) {
        std::cout << "Generating declarations for type...\n";
        t->codegen(cg); // This will now properly resolve parent fields
    }
    
    // Then, generate function declarations
    for (auto* f : functions) {
        f->generateDeclarations(cg);
    }
    
    // Finally, generate other declarations from statements
    for (auto* s : statements) {
        s->generateDeclarations(cg);
    }
}

llvm::Value* Program::generateExecutableCode(CodeGenerator& cg, bool onlyFunctions) {
    // Primero, genera los cuerpos de todas las funciones si onlyFunctions
    if (onlyFunctions) {
        // Procesa todas las funciones declaradas explícitamente
        std::set<FunctionDeclaration*> processed_functions;
        for (auto* f : functions) {
            f->generateExecutableCode(cg, onlyFunctions);
            processed_functions.insert(f);
        }
        
        // Busca funciones que puedan estar en statements pero no en functions
        for (auto* s : statements) {
            if (auto* func_decl = dynamic_cast<FunctionDeclaration*>(s)) {
                if (processed_functions.find(func_decl) == processed_functions.end()) {
                    func_decl->generateExecutableCode(cg, onlyFunctions);
                }
            }
        }
        return nullptr;
    }
    
    // Luego, ejecuta los statements principales (main)
    llvm::Value* lastValue = nullptr;
    bool has_executable_statement = false;
    
    for (auto* s : statements) {
        if (!s) continue;
        
        // Salta las declaraciones de funciones en esta fase
        if (dynamic_cast<FunctionDeclaration*>(s)) {
            continue;
        }
        
        has_executable_statement = true;
        lastValue = s->generateExecutableCode(cg, onlyFunctions);
        
        // Si un bloque ya tiene terminador, no continúes
        llvm::BasicBlock* currentBlock = cg.getBuilder()->GetInsertBlock();
        if (currentBlock && currentBlock->getTerminator()) {
            break;
        }
    }
    
    // Si no hay statements ejecutables o ninguno produjo un valor,
    // devuelve un valor por defecto en lugar de nullptr
    if (lastValue == nullptr) {
        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(cg.getContext()), 0, true);
    }
    
    return lastValue;
}

bool Program::checkCircularInheritance(IContext* context) {
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> visiting;
    
    // Check each type for circular inheritance
    for (auto* t : types) {
        if (visited.find(t->getName()) == visited.end()) {
            if (hasCircularInheritanceHelper(t->getName(), context, visiting, visited)) {
                return false; // Circular inheritance detected
            }
        }
    }
    
    return true; // No circular inheritance found
}

bool Program::hasCircularInheritanceHelper(const std::string& typeName, IContext* context, 
                                          std::unordered_set<std::string>& visiting, 
                                          std::unordered_set<std::string>& visited) {
    // If we're currently visiting this type, we found a cycle
    if (visiting.find(typeName) != visiting.end()) {
        // Find the TypeDefinition for this type to get its location
        TypeDefinition* typeDefForError = nullptr;
        for (auto* t : types) {
            if (t->getName() == typeName) {
                typeDefForError = t;
                break;
            }
        }
        
        // Build the inheritance chain for better error reporting
        std::string inheritanceChain = "";
        std::string current = typeName;
        inheritanceChain += current;
        do {
            std::string parent = context->getParentType(current);
            if (parent != "Object" && parent != current) {
                inheritanceChain += " -> " + parent;
                current = parent;
            } else {
                break;
            }
        } while (current != typeName && visiting.find(current) == visiting.end());
        inheritanceChain += " -> " + typeName + " (circular)";
        
        // Report error with proper location information
        if (typeDefForError) {
            SEMANTIC_ERROR("Circular inheritance detected involving type '" + typeName + "'\nInheritance chain: " + inheritanceChain, typeDefForError->getLocation());
        } else {
            // Fallback to stderr if we can't find the type definition
            std::cerr << "Error: Circular inheritance detected involving type '" << typeName << "'" << std::endl;
            std::cerr << "Inheritance chain: " << inheritanceChain << std::endl;
        }
        
        return true;
    }
    
    // If already visited, no need to check again
    if (visited.find(typeName) != visited.end()) {
        return false;
    }
    
    // Mark as currently visiting
    visiting.insert(typeName);
    
    // Get parent type
    std::string parentType = context->getParentType(typeName);
    
    // Check if parent type exists (unless it's Object)
    if (parentType != "Object") {
        // Find the TypeDefinition for this type to get its location
        TypeDefinition* currentTypeDef = nullptr;
        for (auto* t : types) {
            if (t->getName() == typeName) {
                currentTypeDef = t;
                break;
            }
        }
        
        // First check if trying to inherit from builtin types
        if (parentType == "Number" || parentType == "String" || parentType == "Boolean") {
            if (currentTypeDef) {
                SEMANTIC_ERROR("Cannot inherit from builtin type '" + parentType + "'", currentTypeDef->getLocation());
            } else {
                std::cerr << "Error: Cannot inherit from builtin type '" << parentType << "'" << std::endl;
            }
            visiting.erase(typeName);
            return true;
        }
        
        // Check if parent type is defined
        bool parentExists = false;
        for (auto* t : types) {
            if (t->getName() == parentType) {
                parentExists = true;
                break;
            }
        }
        
        if (!parentExists) {
            if (currentTypeDef) {
                SEMANTIC_ERROR("Parent type '" + parentType + "' of type '" + typeName + "' is not defined", currentTypeDef->getLocation());
            } else {
                std::cerr << "Error: Parent type '" << parentType << "' of type '" << typeName << "' is not defined" << std::endl;
            }
            visiting.erase(typeName);
            return true; // Treat undefined parent as error
        }
        
        // Recursively check parent
        if (hasCircularInheritanceHelper(parentType, context, visiting, visited)) {
            visiting.erase(typeName);
            return true;
        }
    }
    
    // Mark as visited and remove from visiting
    visiting.erase(typeName);
    visited.insert(typeName);
    
    return false;
}

std::vector<TypeDefinition*> Program::topologicalSortTypes() {
    std::vector<TypeDefinition*> result;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> visiting;
    
    // Create a map from type name to TypeDefinition for easy lookup
    std::unordered_map<std::string, TypeDefinition*> typeMap;
    for (auto* t : types) {
        typeMap[t->getName()] = t;
    }
    
    // Helper function for DFS
    std::function<bool(const std::string&)> dfs = [&](const std::string& typeName) -> bool {
        if (visiting.find(typeName) != visiting.end()) {
            // Circular dependency detected
            return false;
        }
        if (visited.find(typeName) != visited.end()) {
            // Already processed
            return true;
        }
        
        visiting.insert(typeName);
        
        // Find the type definition
        auto it = typeMap.find(typeName);
        if (it != typeMap.end()) {
            TypeDefinition* typeDef = it->second;
            std::string parentType = typeDef->getParentType();
            
            // If it has a parent (other than Object), process parent first
            if (parentType != "Object" && !parentType.empty()) {
                if (!dfs(parentType)) {
                    return false; // Circular dependency
                }
            }
            
            // Add this type to result
            result.push_back(typeDef);
        }
        
        visiting.erase(typeName);
        visited.insert(typeName);
        return true;
    };
    
    // Process all types
    for (auto* t : types) {
        if (visited.find(t->getName()) == visited.end()) {
            if (!dfs(t->getName())) {
                // Circular dependency detected, fall back to original order
                std::cerr << "Warning: Circular dependency detected in type sorting, using original order" << std::endl;
                return types;
            }
        }
    }
    
    return result;
}
