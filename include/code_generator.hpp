#pragma once

class IContext;
#include "tree.hpp"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <map>
#include <string>

class Node;
class Context; // Adelanta la declaración de Context

class CodeGenerator {
public:
    llvm::LLVMContext* TheContext;
    llvm::Module* TheModule;
    llvm::IRBuilder<>* Builder;
    std::map<std::string, llvm::Value*> NamedValues;
    IContext* contextObject = nullptr;
    std::vector<IContext*> contextStack;
    
    // Method context tracking for base() calls
    std::string currentTypeName;
    std::string currentMethodName;
    
    // Let-in variable context tracking for vector data association
    std::string currentLetInVarName;
    
    // Helper privado para declarar la función range
    llvm::Function* declareRangeFunction();
    void implementRangeFunction();
    void declareAndImplementRangeSizeMethod();
    ~CodeGenerator();
    CodeGenerator(llvm::LLVMContext* extContext, Context* extContextObject);
    void initializeModule();
    
    // Métodos modernos para iteradores únicos
    void implementIteratorFunctions(const std::string& iterName);
    llvm::GlobalVariable* getIteratorGlobalVariable(const std::string& iterName);
    
    // Vector data storage for iteration
    void storeVectorDataForIterator(const std::string& iterName, const std::vector<double>& values);
    bool hasVectorDataForIterator(const std::string& iterName) const;
    std::vector<double> getVectorDataForIterator(const std::string& iterName) const;
    const std::map<std::string, std::vector<double>>& getAllVectorData() const;
    std::map<std::string, std::vector<double>> vectorData;
    
    llvm::LLVMContext& getContext() { return *TheContext; }
    llvm::Module* getModule() { return TheModule; }
    llvm::IRBuilder<>* getBuilder() { return Builder; }

    void initializeUtilityFunctions();
    llvm::Function* getMainFunction();
    
    void setNamedValue(const std::string& name, llvm::Value* value) {
        NamedValues[name] = value;
    }
    
    llvm::Value* getNamedValue(const std::string& name) {
        auto it = NamedValues.find(name);
        if (it != NamedValues.end()) {
            return it->second;
        }
        return nullptr; // Return nullptr if not found
    }
    
    // Versión para StringRef (más eficiente)
    llvm::Value* getNamedValue(llvm::StringRef name) {
        auto it = NamedValues.find(name.str());
        if (it != NamedValues.end()) {
            return it->second;
        }
        return nullptr; // Return nullptr if not found
    }
    
    void removeNamedValue(const std::string& name) {
        NamedValues.erase(name);
    }

    // Method to generate print function calls (now uses printf directly)
    llvm::Value* generatePrintCall(llvm::Value* value); 
    
    llvm::Function* declareSprintf();

    llvm::Value* createStringBuffer(int size);

    llvm::Value* createGlobalString(const std::string& str);
    
    // Convert an integer value to string representation
    llvm::Value* intToString(llvm::Value* intVal);
    
    // Convert a float value to string representation
    llvm::Value* floatToString(llvm::Value* floatVal);
    // ... otras declaraciones ...
    llvm::AllocaInst* createEntryBlockAlloca(llvm::Function* function, llvm::StringRef varName);
    llvm::AllocaInst* createEntryBlockAlloca(llvm::Function* function, const std::string& varName, llvm::Type* type);
    llvm::Value* convertToBoolean(llvm::Value* value);
    llvm::Value* generateBinaryOp(llvm::Value* left, llvm::Value* right, 
                                 const std::string& op);
    
    bool generateCode(Node* root);
    void dumpCode();
    bool saveModuleToFile(const std::string& filename);
    
    // Context object access methods
    void setContextObject(IContext* ctx) { contextObject = ctx; }
    // Return the original global context object, not the current context from the stack
    IContext* getContextObject() { return contextObject; }
    
    // Scope management methods
    void pushScope();
    void popScope();
    IContext* currentContext();
    
    // Method context management for base() calls
    void setCurrentMethod(const std::string& typeName, const std::string& methodName) {
        currentTypeName = typeName;
        currentMethodName = methodName;
    }
    
    std::string getCurrentTypeName() const { return currentTypeName; }
    std::string getCurrentMethodName() const { return currentMethodName; }
    
    // Let-in variable context management for vector data association
    void setCurrentLetInVariableName(const std::string& name) {
        currentLetInVarName = name;
    }
    
    std::string getCurrentLetInVariableName() const {
        return currentLetInVarName;
    }
};
