#ifndef TYPE_SYSTEM_HPP
#define TYPE_SYSTEM_HPP

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>

// Forward declarations
class Expression;

// Enum for built-in types
enum class BuiltinType {
    NUMBER,    // 32-bit float
    STRING,    // String type
    BOOLEAN,   // Boolean type
    VOID,      // Void type
    UNKNOWN    // Unknown/error type
};

// Base class for all types in the type system
class Type {
public:
    virtual ~Type() = default;
    virtual std::string toString() const = 0;
    virtual bool isCompatibleWith(const Type* other) const = 0;
    virtual llvm::Type* toLLVMType(llvm::LLVMContext& context) const = 0;
    virtual bool isBuiltin() const { return false; }
    virtual bool isUserDefined() const { return false; }
    virtual bool isFunction() const { return false; }
};

// Built-in types (Number, String, Boolean, etc.)
class BuiltinTypeImpl : public Type {
private:
    BuiltinType type;

public:
    BuiltinTypeImpl(BuiltinType t) : type(t) {}
    
    std::string toString() const override {
        switch (type) {
            case BuiltinType::NUMBER: return "Number";
            case BuiltinType::STRING: return "String";
            case BuiltinType::BOOLEAN: return "Boolean";
            case BuiltinType::VOID: return "Void";
            case BuiltinType::UNKNOWN: return "Unknown";
        }
        return "Unknown";
    }
    
    bool isCompatibleWith(const Type* other) const override {
        if (auto otherBuiltin = dynamic_cast<const BuiltinTypeImpl*>(other)) {
            return type == otherBuiltin->type;
        }
        return false;
    }
    
    llvm::Type* toLLVMType(llvm::LLVMContext& context) const override {
        switch (type) {
            case BuiltinType::NUMBER: return llvm::Type::getFloatTy(context);
            case BuiltinType::STRING: return llvm::Type::getInt8PtrTy(context);
            case BuiltinType::BOOLEAN: return llvm::Type::getInt1Ty(context);
            case BuiltinType::VOID: return llvm::Type::getVoidTy(context);
            case BuiltinType::UNKNOWN: return nullptr;
        }
        return nullptr;
    }
    
    bool isBuiltin() const override { return true; }
    BuiltinType getBuiltinType() const { return type; }
};

// Function types
class FunctionType : public Type {
private:
    std::vector<std::unique_ptr<Type>> paramTypes;
    std::unique_ptr<Type> returnType;

public:
    FunctionType(std::vector<std::unique_ptr<Type>> params, std::unique_ptr<Type> ret)
        : paramTypes(std::move(params)), returnType(std::move(ret)) {}
    
    std::string toString() const override {
        std::string result = "(";
        for (size_t i = 0; i < paramTypes.size(); i++) {
            if (i > 0) result += ", ";
            result += paramTypes[i]->toString();
        }
        result += ") -> " + returnType->toString();
        return result;
    }
    
    bool isCompatibleWith(const Type* other) const override {
        if (auto otherFunc = dynamic_cast<const FunctionType*>(other)) {
            if (paramTypes.size() != otherFunc->paramTypes.size()) return false;
            for (size_t i = 0; i < paramTypes.size(); i++) {
                if (!paramTypes[i]->isCompatibleWith(otherFunc->paramTypes[i].get())) {
                    return false;
                }
            }
            return returnType->isCompatibleWith(otherFunc->returnType.get());
        }
        return false;
    }
    
    llvm::Type* toLLVMType(llvm::LLVMContext& context) const override {
        std::vector<llvm::Type*> llvmParams;
        for (const auto& param : paramTypes) {
            llvmParams.push_back(param->toLLVMType(context));
        }
        return llvm::FunctionType::get(returnType->toLLVMType(context), llvmParams, false);
    }
    
    bool isFunction() const override { return true; }
    
    const std::vector<std::unique_ptr<Type>>& getParamTypes() const { return paramTypes; }
    const Type* getReturnType() const { return returnType.get(); }
};

// User-defined types (classes)
class UserDefinedType : public Type {
private:
    std::string name;
    std::map<std::string, std::unique_ptr<Type>> fields;
    std::map<std::string, std::unique_ptr<FunctionType>> methods;

public:
    UserDefinedType(const std::string& n) : name(n) {}
    
    std::string toString() const override { return name; }
    
    bool isCompatibleWith(const Type* other) const override {
        if (auto otherUser = dynamic_cast<const UserDefinedType*>(other)) {
            return name == otherUser->name;
        }
        return false;
    }
    
    llvm::Type* toLLVMType(llvm::LLVMContext& context) const override {
        // This would need to be implemented based on the struct layout
        return llvm::StructType::create(context, name);
    }
    
    bool isUserDefined() const override { return true; }
    
    void addField(const std::string& fieldName, std::unique_ptr<Type> fieldType) {
        fields[fieldName] = std::move(fieldType);
    }
    
    void addMethod(const std::string& methodName, std::unique_ptr<FunctionType> methodType) {
        methods[methodName] = std::move(methodType);
    }
    
    const Type* getFieldType(const std::string& fieldName) const {
        auto it = fields.find(fieldName);
        return it != fields.end() ? it->second.get() : nullptr;
    }
    
    const FunctionType* getMethodType(const std::string& methodName) const {
        auto it = methods.find(methodName);
        return it != methods.end() ? it->second.get() : nullptr;
    }
    
    // Getter for the type name
    const std::string& getName() const { return name; }
};

// Type registry for managing all types in the system
class TypeRegistry {
private:
    std::map<std::string, std::unique_ptr<Type>> types;
    class IContext* context; // Store context reference for dynamic type creation
    
public:
    TypeRegistry() : context(nullptr) {
        // Register built-in types
        types["Number"] = std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER);
        types["String"] = std::make_unique<BuiltinTypeImpl>(BuiltinType::STRING);
        types["Boolean"] = std::make_unique<BuiltinTypeImpl>(BuiltinType::BOOLEAN);
        types["Void"] = std::make_unique<BuiltinTypeImpl>(BuiltinType::VOID);
    }
    
    TypeRegistry(class IContext* ctx) : context(ctx) {
        // Register built-in types
        types["Number"] = std::make_unique<BuiltinTypeImpl>(BuiltinType::NUMBER);
        types["String"] = std::make_unique<BuiltinTypeImpl>(BuiltinType::STRING);
        types["Boolean"] = std::make_unique<BuiltinTypeImpl>(BuiltinType::BOOLEAN);
        types["Void"] = std::make_unique<BuiltinTypeImpl>(BuiltinType::VOID);
    }
    
    const Type* getType(const std::string& name) const;
    
    void registerType(const std::string& name, std::unique_ptr<Type> type) {
        types[name] = std::move(type);
    }
    
    bool isValidType(const std::string& name) const {
        return types.find(name) != types.end();
    }
    
    // Helper methods for common types
    const Type* getNumberType() const { return getType("Number"); }
    const Type* getStringType() const { return getType("String"); }
    const Type* getBooleanType() const { return getType("Boolean"); }
    const Type* getVoidType() const { return getType("Void"); }
};

// Type inference and checking utilities
class TypeChecker {
private:
    TypeRegistry& registry;
    
public:
    TypeChecker(TypeRegistry& reg) : registry(reg) {}
    
    // Infer the type of an expression
    const Type* inferType(Expression* expr, class IContext* context);
    
    // Infer the return type of a method by analyzing its body
    const Type* inferMethodReturnType(const std::string& typeName, const std::string& methodName, class IContext* context);
    
    // Find common ancestor of two types in the inheritance hierarchy
    std::string findCommonAncestor(const std::string& type1, const std::string& type2, class IContext* context);
    
    // Infer the type of an IfStatement when used as an expression
    const Type* inferIfStatementType(class IfStatement* ifStmt, class IContext* context);
    
    // Check if two types are compatible (including inheritance)
    bool areTypesCompatible(const Type* expected, const Type* actual) const;
    
    // Check if two types are compatible with inheritance support
    bool areTypesCompatibleWithInheritance(const Type* expected, const Type* actual, class IContext* context) const;
    
    // Check function call compatibility
    bool checkFunctionCall(const FunctionType* funcType, const std::vector<const Type*>& argTypes) const {
        if (funcType->getParamTypes().size() != argTypes.size()) return false;
        
        for (size_t i = 0; i < argTypes.size(); i++) {
            if (!areTypesCompatible(funcType->getParamTypes()[i].get(), argTypes[i])) {
                return false;
            }
        }
        return true;
    }
};

#endif // TYPE_SYSTEM_HPP