#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <set>
#include <error_handler.hpp>

Context::Context(IContext* parent, llvm::LLVMContext* llvmContext)
    : parent(parent), llvmContext(llvmContext) {
    if (parent == nullptr) {
        initializeBuiltins();
    }
}

Context::~Context() {}

llvm::LLVMContext* Context::getLLVMContext() {
    return llvmContext;
}

void Context::initializeBuiltins() {
    // Add print as a predefined function that accepts any number of arguments
    // Empty parameter vector indicates it accepts any number of arguments
    functions["print"] = std::vector<std::string>();

    functions["range"] = std::vector<std::string>{"start", "end"};
    
    // Add math functions (integer versions)
    functions["sqrt_i32"] = std::vector<std::string>{"x"};
    functions["sin_i32"] = std::vector<std::string>{"x"};
    functions["cos_i32"] = std::vector<std::string>{"x"};
    
    // Also register the original names to map to integer versions
    functions["sqrt"] = std::vector<std::string>{"x"};
    functions["sin"] = std::vector<std::string>{"x"};
    functions["cos"] = std::vector<std::string>{"x"};
    
    // Add rand function (no parameters)
    functions["rand"] = std::vector<std::string>();
    
    // Register predefined types for 'is' expressions
    // These are registered with nullptr as they are built-in types without struct definitions
    types["Object"] = nullptr;    // Base type for all objects
    types["Number"] = nullptr;    // Built-in numeric type
    types["String"] = nullptr;    // Built-in string type
    types["Boolean"] = nullptr;   // Built-in boolean type
}

bool Context::isDefined(const std::string& name) {
    return variables.find(name) != variables.end() || 
           (parent && parent->isDefined(name));
}

bool Context::isDefined(std::string function, int args) {
    auto it = functions.find(function);
    if (it != functions.end()) {
        // Special case for print: accepts any number of arguments
        if (function == "print") {
            return true;
        }
        return it->second.size() == static_cast<size_t>(args);
    }
    return parent ? parent->isDefined(function, args) : false;
}

bool Context::addVariable(Variable* var) {
    // Inserta un par <nombre, VariableInfo>
    return variables.insert({var->name, VariableInfo()}).second;
}

bool Context::addFunction(std::string function, const std::vector<std::string>& params) {
    if (functions.find(function) != functions.end()) {
        if (functions[function].size() == params.size()) {
            return false;
        }
    }
    functions[function] = params;
    return true;
}

bool Context::addFunction(const std::string& name, const std::vector<std::string>& params, 
                         const std::vector<std::string>& paramTypes, const std::string& returnType) {
    // Check if function already exists with same signature
    auto it = typedFunctions.find(name);
    if (it != typedFunctions.end()) {
        if (it->second.params.size() == params.size()) {
            return false; // Function already exists with same arity
        }
    }
    
    FunctionInfo info;
    info.params = params;
    info.paramTypes = paramTypes;
    info.returnType = returnType;
    typedFunctions[name] = info;
    
    // Also add to legacy functions for compatibility
    functions[name] = params;
    return true;
}

bool Context::checkFunctionCall(const std::string& name, const std::vector<std::string>& argTypes) {
    // First check typed functions
    auto it = typedFunctions.find(name);
    if (it != typedFunctions.end()) {
        const FunctionInfo& funcInfo = it->second;
        
        // Check arity
        if (funcInfo.paramTypes.size() != argTypes.size()) {
            std::cout << "[DEBUG] Function " << name << " arity mismatch: expected " 
                      << funcInfo.paramTypes.size() << ", got " << argTypes.size() << std::endl;
            return false;
        }
        
        // Check type compatibility
        for (size_t i = 0; i < argTypes.size(); i++) {
            std::cout << "[DEBUG] Checking parameter " << i << ": expected " 
                      << funcInfo.paramTypes[i] << ", got " << argTypes[i] << std::endl;
            
            if (funcInfo.paramTypes[i] != argTypes[i]) {
                // No implicit conversions allowed for strict type checking
                std::cout << "[DEBUG] Type mismatch for parameter " << i << std::endl;
                return false;
            }
        }
        std::cout << "[DEBUG] Function " << name << " type check passed" << std::endl;
        return true;
    }
    
    // Check parent context
    if (parent) {
        return parent->checkFunctionCall(name, argTypes);
    }
    
    // For built-in functions like print, allow any types
    if (name == "print") {
        return true;
    }
    
    std::cout << "[DEBUG] Function " << name << " not found in typed functions" << std::endl;
    return false;
}

std::vector<std::string> Context::getFunctionParamTypes(const std::string& name) {
    // Check typed functions first
    auto it = typedFunctions.find(name);
    if (it != typedFunctions.end()) {
        return it->second.paramTypes;
    }
    
    // Check parent context
    if (parent) {
        return parent->getFunctionParamTypes(name);
    }
    
    // Return empty vector if not found
    return std::vector<std::string>();
}

std::string Context::getFunctionReturnType(const std::string& name) {
    // Check typed functions first
    auto it = typedFunctions.find(name);
    if (it != typedFunctions.end()) {
        return it->second.returnType;
    }
    
    // Check parent context
    if (parent) {
        return parent->getFunctionReturnType(name);
    }
    
    // Return empty string if not found
    return "";
}

IContext* Context::createChildContext() {
    // Usa el mismo llvmContext del padre
    return new Context(this, llvmContext);
}

bool Context::addVariable(const std::string& name, llvm::Type* type) {
    if (variables.count(name)) return false;
    variables[name] = {type};
    // Also store in variableTypes for getVariableType() method
    if (type) {
        variableTypes[name] = type;
    }
    return true;
}

bool Context::addVariable(const std::string& name, llvm::Type* type, const std::string& typeName) {
    if (variables.count(name)) return false;
    variables[name] = {type};
    // Also store in variableTypes for getVariableType() method
    if (type) {
        variableTypes[name] = type;
    }
    // Store the semantic type name
    variableTypeNames[name] = typeName;
    return true;
}

std::string Context::getVariableTypeName(const std::string& name) {
    auto it = variableTypeNames.find(name);
    if (it != variableTypeNames.end()) {
        return it->second;
    }
    return parent ? parent->getVariableTypeName(name) : "";
}

void Context::setVariableTypeName(const std::string& name, const std::string& typeName) {
    variableTypeNames[name] = typeName;
}

// Object-oriented features implementation
bool Context::addType(const std::string& name, llvm::StructType* type) {
    auto it = types.find(name);
    if (it != types.end()) {
        // Type already exists - update it if the new type is not null
        if (type != nullptr) {
            types[name] = type;
            return true;
        } else {
            return false; // Don't overwrite with null
        }
    }
    types[name] = type;
    return true;
}

llvm::StructType* Context::getType(const std::string& name) {
    std::cout << "[DEBUG] Context::getType called for '" << name << "' on context " << this << std::endl;
    
    // Add null check for this pointer
    if (this == nullptr) {
        std::cerr << "[ERROR] Context::getType called with null this pointer!" << std::endl;
        return nullptr;
    }
    
    // First check current context
    auto it = types.find(name);
    if (it != types.end()) {
        std::cout << "[DEBUG] Found type '" << name << "' in current context" << std::endl;
        return it->second;
    }
    
    // Then check parent context, but avoid infinite recursion by limiting depth
    static thread_local int recursionDepth = 0;
    const int MAX_RECURSION_DEPTH = 10;
    
    if (parent && recursionDepth < MAX_RECURSION_DEPTH) {
        std::cout << "[DEBUG] Type '" << name << "' not found in current context, checking parent" << std::endl;
        recursionDepth++;
        llvm::StructType* result = parent->getType(name);
        recursionDepth--;
        return result;
    } else if (recursionDepth >= MAX_RECURSION_DEPTH) {
        std::cerr << "[ERROR] Maximum recursion depth reached in context chain for type '" << name << "'" << std::endl;
        return nullptr;
    } else {
        std::cout << "[DEBUG] Type '" << name << "' not found and no parent context" << std::endl;
        return nullptr;
    }
}

bool Context::hasType(const std::string& name) {
    auto it = types.find(name);
    if (it != types.end()) {
        return true; // Type is registered, regardless of whether struct is nullptr
    }
    if (parent) {
        return parent->hasType(name);
    } else {
        return false;
    }
}

bool Context::addFieldIndex(const std::string& typeName, const std::string& fieldName, int index) {
    fieldIndices[typeName][fieldName] = index;
    
    // Also add to fieldNames if needed
    if (fieldNames[typeName].size() <= static_cast<size_t>(index)) {
        fieldNames[typeName].resize(index + 1);
    }
    fieldNames[typeName][index] = fieldName;
    
    return true;
}

int Context::getFieldIndex(const std::string& typeName, const std::string& fieldName) {
    auto typeIt = fieldIndices.find(typeName);
    if (typeIt != fieldIndices.end()) {
        auto fieldIt = typeIt->second.find(fieldName);
        if (fieldIt != typeIt->second.end()) {
            return fieldIt->second;
        }
    }
    return parent ? parent->getFieldIndex(typeName, fieldName) : -1;
}

std::string Context::getFieldName(const std::string& typeName, int index) {
    auto it = fieldNames.find(typeName);
    if (it != fieldNames.end() && index >= 0 && index < static_cast<int>(it->second.size())) {
        return it->second[index];
    }
    return parent ? parent->getFieldName(typeName, index) : "";
}

int Context::getFieldCount(const std::string& typeName) {
    auto it = fieldNames.find(typeName);
    if (it != fieldNames.end()) {
        return static_cast<int>(it->second.size());
    }
    return parent ? parent->getFieldCount(typeName) : 0;
}

// Inheritance support implementation
bool Context::setParentType(const std::string& childType, const std::string& parentType) {
    parentTypes[childType] = parentType;
    return true;
}

std::string Context::getParentType(const std::string& typeName) {
    auto it = parentTypes.find(typeName);
    if (it != parentTypes.end()) {
        return it->second;
    }
    return parent ? parent->getParentType(typeName) : "Object";
}

bool Context::isSubtypeOf(const std::string& childType, const std::string& parentType) {
    if (childType == parentType) {
        return true;
    }
    
    // Special case: all types inherit from Object implicitly
    if (parentType == "Object") {
        return true;
    }
    
    std::string currentType = childType;
    while (currentType != "Object") {
        std::string parentOfCurrent = getParentType(currentType);
        if (parentOfCurrent == parentType) {
            return true;
        }
        currentType = parentOfCurrent;
    }
    
    return false;
}

bool Context::addMethod(const std::string& typeName, const std::string& methodName, llvm::Function* func) {
    methods[typeName][methodName] = func;
    return true;
}

llvm::Function* Context::getMethod(const std::string& typeName, const std::string& methodName) {
    // Walk up the inheritance chain to find the method
    std::string currentType = typeName;
    while (currentType != "Object") {
        auto typeIt = methods.find(currentType);
        if (typeIt != methods.end()) {
            auto methodIt = typeIt->second.find(methodName);
            if (methodIt != typeIt->second.end()) {
                return methodIt->second;
            }
        }
        // Move to parent type
        std::string parentType = getParentType(currentType);
        if (parentType == currentType || parentType.empty()) {
            break;
        }
        currentType = parentType;
    }
    // Check in parent context (for global methods, if any)
    return parent ? parent->getMethod(typeName, methodName) : nullptr;
}

// Register a type definition (call this in TypeDefinition::codegen or similar)
void Context::registerTypeDefinition(const std::string& typeName, TypeDefinition* typeDef) {
    typeDefinitions[typeName] = typeDef;
}

TypeDefinition* Context::getTypeDefinition(const std::string& typeName) {
    auto it = typeDefinitions.find(typeName);
    if (it != typeDefinitions.end()) {
        return it->second;
    }
    return parent ? parent->getTypeDefinition(typeName) : nullptr;
}

// Current type tracking implementation
void Context::setCurrentType(const std::string& typeName) {
    currentType = typeName;
}

std::string Context::getCurrentType() {
    if (!currentType.empty()) {
        return currentType;
    }
    return parent ? parent->getCurrentType() : "";
}