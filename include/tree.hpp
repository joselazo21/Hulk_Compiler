#ifndef TREE_HPP
#define TREE_HPP

#include <string>
#include <vector>
#include <utility>
#include <unordered_set>
#include <map>
#include <error_handler.hpp>
#include "code_generator.hpp"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>

// Forward declarations
class Node;
class Statement;
class BlockStatement;
class Expression;
class ExpressionStatement;
class Variable;
class IContext;
class CodeGenerator;
class MemberAccess;
// --- Add forward declaration for TypeDefinition to fix error ---
class TypeDefinition;

// Función de utilidad para indentación
void printIndent(int depth);

// Base Node class
class Node {
protected:
    SourceLocation location;
    
public:
    Node(const SourceLocation& loc = SourceLocation());
    virtual void printNode(int depth) = 0;
    virtual bool Validate(IContext* context) = 0;
    virtual llvm::Value* codegen(CodeGenerator& generator) = 0;

    // --- NUEVO: Separación de declaraciones y código ejecutable ---
    virtual void generateDeclarations(CodeGenerator& cg) { /* por defecto nada */ }
    virtual llvm::Value* generateExecutableCode(CodeGenerator& cg, bool onlyFunctions = false) { return codegen(cg); }
    virtual ~Node() = default;
    
    const SourceLocation& getLocation() const { return location; }
};

// Statement class
class Statement : public Node {
public:
    Statement(const SourceLocation& loc = SourceLocation());
    virtual void printNode(int depth) override;
    virtual bool Validate(IContext* context) override;
    virtual llvm::Value* codegen(CodeGenerator& generator) override = 0;
    virtual ~Statement() = default;

    // Añadido para obtener la última variable asignada (por defecto nullptr)
    virtual const std::string* getLastAssignedVariable() const { return nullptr; }
};

// BlockStatement class
class BlockStatement : public Statement {
private:
    std::vector<Statement*> statements;

public:
    BlockStatement(const SourceLocation& loc, const std::vector<Statement*>& stmts);
    BlockStatement(const SourceLocation& loc = SourceLocation()); // opcional
    void addStatement(Statement* stmt);
    const std::vector<Statement*>& getStatements() const;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~BlockStatement();

    // --- NUEVO ---
    void generateDeclarations(CodeGenerator& cg) override;
    llvm::Value* generateExecutableCode(CodeGenerator& cg, bool onlyFunctions = false) override;
    
    // Method to get the last expression for return type checking
    Expression* getLastExpression() const;
    
    // Method to get the last statement for type checking (including IfStatements)
    Statement* getLastStatement() const;
};

// ExpressionStatement class
class ExpressionStatement : public Statement {
private:
    Expression* expr;

public:
    ExpressionStatement(Expression* e);
    ExpressionStatement(const SourceLocation& loc, Expression* e);
    Expression* getExpression() const { return expr; }
    virtual void printNode(int depth) override;
    virtual bool Validate(IContext* context) override;
    virtual llvm::Value* codegen(CodeGenerator& generator) override; // Always returns a value
    virtual ~ExpressionStatement();
};

// Expression base class
class Expression : public Node {
public:
    Expression(const SourceLocation& loc = SourceLocation());
    virtual ~Expression() = default;
    virtual std::string toString() = 0;
    virtual void printNode(int depth) override = 0;
    virtual bool Validate(IContext* context) override = 0;
    virtual llvm::Value* codegen(CodeGenerator& generator) override = 0;
};

// Variable class
class Variable : public Expression {        
public:
    std::string name;
    friend class IContext;
    Variable(const std::string& name);
    Variable(const SourceLocation& loc, const std::string& name);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~Variable();
    
    const std::string& getName() const { return name; }
};

// IContext interface
class IContext {
public:
    virtual bool isDefined(const std::string& name) = 0;
    virtual bool isDefined(std::string function, int args) = 0;
    virtual bool addVariable(const std::string& name, llvm::Type* type = nullptr) = 0;
    virtual bool addVariable(const std::string& name, llvm::Type* type, const std::string& typeName) = 0;
    virtual std::string getVariableTypeName(const std::string& name) = 0;
    virtual void setVariableTypeName(const std::string& name, const std::string& typeName) = 0;
    virtual bool addFunction(std::string function, const std::vector<std::string>& params) = 0;
    virtual bool addFunction(const std::string& name, const std::vector<std::string>& params, 
                           const std::vector<std::string>& paramTypes, const std::string& returnType) = 0;
    virtual bool checkFunctionCall(const std::string& name, const std::vector<std::string>& argTypes) = 0;
    virtual std::vector<std::string> getFunctionParamTypes(const std::string& name) = 0;
    virtual std::string getFunctionReturnType(const std::string& name) = 0;
    virtual IContext* createChildContext() = 0;
    virtual llvm::Type* getVariableType(const std::string& name) = 0;
    virtual void setVariableType(const std::string& name, llvm::Type* type) = 0;
    virtual llvm::LLVMContext* getLLVMContext() = 0;
    virtual bool addVariable(class Variable* var) = 0;

    // --- Object-oriented features ---
    virtual bool addType(const std::string& name, llvm::StructType* type) = 0;
    virtual llvm::StructType* getType(const std::string& name) = 0;
    virtual bool hasType(const std::string& name) = 0; // Check if type name is registered
    virtual bool addFieldIndex(const std::string& typeName, const std::string& fieldName, int index) = 0;
    virtual int getFieldIndex(const std::string& typeName, const std::string& fieldName) = 0;
    virtual std::string getFieldName(const std::string& typeName, int index) = 0;
    virtual int getFieldCount(const std::string& typeName) = 0;

    // --- Inheritance support ---
    virtual bool setParentType(const std::string& childType, const std::string& parentType) = 0;
    virtual std::string getParentType(const std::string& typeName) = 0;
    virtual bool isSubtypeOf(const std::string& childType, const std::string& parentType) = 0;
    virtual bool addMethod(const std::string& typeName, const std::string& methodName, llvm::Function* func) = 0;
    virtual llvm::Function* getMethod(const std::string& typeName, const std::string& methodName) = 0;

    // Add this method for type lookup by name (for AST access)
    virtual TypeDefinition* getTypeDefinition(const std::string& typeName) = 0;

    // Methods to track the current type being processed (for self expressions)
    virtual void setCurrentType(const std::string& typeName) = 0;
    virtual std::string getCurrentType() = 0;

    virtual ~IContext() = default;
};

class Context : public IContext {
private:
    struct VariableInfo {
        llvm::Type* type;
        // Add other variable properties as needed
    };
    
    struct FunctionInfo {
        std::vector<std::string> params;
        std::vector<std::string> paramTypes;
        std::string returnType;
    };
    
    std::map<std::string, VariableInfo> variables;
    std::map<std::string, llvm::Type*> variableTypes;
    std::map<std::string, std::string> variableTypeNames; // Map variable names to semantic type names
    std::map<std::string, std::vector<std::string>> functions; // Legacy support
    std::map<std::string, FunctionInfo> typedFunctions; // New typed functions
    IContext* parent;
    llvm::LLVMContext* llvmContext;

    std::map<std::string, llvm::StructType*> types;
    std::map<std::string, std::map<std::string, int>> fieldIndices;
    std::map<std::string, std::vector<std::string>> fieldNames;

    std::map<std::string, std::string> parentTypes;
    std::map<std::string, std::map<std::string, llvm::Function*>> methods;

    void initializeBuiltins();

    // --- Add storage for type definitions ---
    std::map<std::string, TypeDefinition*> typeDefinitions;

    // --- Add current type tracking ---
    std::string currentType;

public:
    bool addVariable(const std::string& name, llvm::Type* type = nullptr) override;
    bool addVariable(const std::string& name, llvm::Type* type, const std::string& typeName) override;
    std::string getVariableTypeName(const std::string& name) override;
    void setVariableTypeName(const std::string& name, const std::string& typeName) override;
    bool isDefined(const std::string& name) override;
    bool isDefined(std::string function, int args) override;
    bool addVariable(Variable* var) override;
    bool addFunction(std::string function, const std::vector<std::string>& params) override;
    bool addFunction(const std::string& name, const std::vector<std::string>& params, 
                   const std::vector<std::string>& paramTypes, const std::string& returnType) override;
    bool checkFunctionCall(const std::string& name, const std::vector<std::string>& argTypes) override;
    std::vector<std::string> getFunctionParamTypes(const std::string& name) override;
    std::string getFunctionReturnType(const std::string& name) override;
    IContext* createChildContext() override;
    llvm::Type* getVariableType(const std::string& name) override;
    void setVariableType(const std::string& name, llvm::Type* type) override;
    Context(IContext* parent = nullptr, llvm::LLVMContext* llvmContext = nullptr);
    llvm::LLVMContext* getLLVMContext() override;
    ~Context();
    
    // --- Object-oriented features implementation ---
    bool addType(const std::string& name, llvm::StructType* type) override;
    llvm::StructType* getType(const std::string& name) override;
    bool hasType(const std::string& name) override;
    bool addFieldIndex(const std::string& typeName, const std::string& fieldName, int index) override;
    int getFieldIndex(const std::string& typeName, const std::string& fieldName) override;
    std::string getFieldName(const std::string& typeName, int index) override;
    int getFieldCount(const std::string& typeName) override;
    
    // --- Inheritance support implementation ---
    bool setParentType(const std::string& childType, const std::string& parentType) override;
    std::string getParentType(const std::string& typeName) override;
    bool isSubtypeOf(const std::string& childType, const std::string& parentType) override;
    bool addMethod(const std::string& typeName, const std::string& methodName, llvm::Function* func) override;
    llvm::Function* getMethod(const std::string& typeName, const std::string& methodName) override;

    // Add this override
    TypeDefinition* getTypeDefinition(const std::string& typeName) override;

    // Add a method to register a type definition
    void registerTypeDefinition(const std::string& typeName, TypeDefinition* typeDef);

    // Current type tracking implementation
    void setCurrentType(const std::string& typeName) override;
    std::string getCurrentType() override;
    
    // Get all type definitions
    const std::map<std::string, TypeDefinition*>& getTypeDefinitions() const;
};

// Implementation of the new type-related methods
inline llvm::Type* Context::getVariableType(const std::string& name) {
    auto it = variableTypes.find(name);
    if (it != variableTypes.end()) return it->second;
    return parent ? parent->getVariableType(name) : nullptr;
}

inline void Context::setVariableType(const std::string& name, llvm::Type* type) {
    variableTypes[name] = type;
}
// Binary Operation class
class BinaryOperation : public Expression {
private:
    Expression* left;
    Expression* right;
    std::string operation;

public:
    BinaryOperation(Expression* left, Expression* right, std::string operation);
    BinaryOperation(const SourceLocation& loc, Expression* left, Expression* right, std::string operation);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~BinaryOperation();
    
    // Getters for type checking
    const std::string& getOperation() const { return operation; }
    Expression* getLeft() const { return left; }
    Expression* getRight() const { return right; }
};

// Unary Operation class
class UnaryOperation : public Expression {
private:
    Expression* operand;
    std::string operation;

public:
    UnaryOperation(Expression* operand, std::string operation);
    UnaryOperation(const SourceLocation& loc, Expression* operand, std::string operation);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~UnaryOperation();
    
    // Getters for type checking
    const std::string& getOperation() const { return operation; }
    Expression* getOperand() const { return operand; }
};

// Number class
class Number : public Expression {
private:
    double value;

public:
    Number(double value);
    Number(const SourceLocation& loc, double value);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~Number();
    
    double getValue() const { return value; }
};

// Boolean class
class Boolean : public Expression {
private:
    bool value;

public:
    Boolean(bool value);
    Boolean(const SourceLocation& loc, bool value);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~Boolean();
    
    bool getValue() const { return value; }
};

// Assignment class
class Assignment : public Statement {
private:
    Expression* expr;
    std::string id;

public:
    Assignment(const std::string& id, Expression* expr);
    Assignment(const SourceLocation& loc, const std::string& id, Expression* expr);
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~Assignment();

    // Implementación para devolver el id de la variable asignada
    const std::string* getLastAssignedVariable() const override { return &id; }
};

// AssignmentExpression class
class AssignmentExpression : public Expression {
private:
    std::string id;
    Expression* expr;
    MemberAccess* memberAccess; // Added for member access assignment
    
    // Helper functions for type checking
    const class Type* inferMemberType(MemberAccess* memberAccess, IContext* context, class TypeRegistry& typeRegistry);
    const class Type* llvmTypeToOurType(llvm::Type* llvmType, class TypeRegistry& typeRegistry);

public:
    AssignmentExpression(const std::string& id, Expression* expr);
    AssignmentExpression(const SourceLocation& loc, const std::string& id, Expression* expr);
    AssignmentExpression(const SourceLocation& loc, MemberAccess* memberAccess, Expression* expr); // Added constructor
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~AssignmentExpression();
    
    // Getter for type checking
    Expression* getExpression() const { return expr; }
};

// Print class
class Print : public Expression {
private:
    Expression* expr;

public:
    Print(Expression* expr);
    Print(const SourceLocation& loc, Expression* expr);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~Print();
};

// StringLiteral class
class StringLiteral : public Expression {
private:
    std::string value;

public:
    StringLiteral(const std::string& value);
    StringLiteral(const SourceLocation& loc, const std::string& value);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~StringLiteral();
};

// FunctionCall class
class FunctionCall : public Expression {
private:
    std::string func_name;
    std::vector<Expression*> args;
    
    // Helper functions for type checking
    const class FunctionType* getFunctionType(const std::string& funcName, IContext* context, class TypeRegistry& typeRegistry);
    bool isBuiltinFunction(const std::string& funcName);

public:
    FunctionCall(const std::string& name, const std::vector<Expression*>& args);
    FunctionCall(const SourceLocation& loc, const std::string& name, const std::vector<Expression*>& args);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~FunctionCall();
    
    // Getter for function name
    const std::string& getFunctionName() const { return func_name; }
};

// IfStatement class
class IfStatement : public Statement {
private:
    Expression* condition;
    BlockStatement* thenBranch;
    BlockStatement* elseBranch;

public:
    IfStatement(Expression* condition, BlockStatement* thenBranch, BlockStatement* elseBranch);
    IfStatement(const SourceLocation& loc, Expression* condition, BlockStatement* thenBranch, BlockStatement* elseBranch);
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~IfStatement();
    
    // Getter methods for type checking
    Expression* getCondition() const { return condition; }
    BlockStatement* getThenBranch() const { return thenBranch; }
    BlockStatement* getElseBranch() const { return elseBranch; }
};

// WhileStatement class
class WhileStatement : public Statement {
private:
    Expression* condition;
    BlockStatement* body;

public:
    WhileStatement(Expression* condition, BlockStatement* body);
    WhileStatement(const SourceLocation& loc, Expression* condition, BlockStatement* body);
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~WhileStatement();

    // Métodos de acceso para el parser
    Expression* getCondition() const { return condition; }
    BlockStatement* getBody() const { return body; }
};

// ForStatement class
class ForStatement : public Statement {
private:
    Statement* init;
    Expression* condition;
    Statement* increment;
    BlockStatement* body;

public:
    ForStatement(Statement* init, Expression* condition, Statement* increment, BlockStatement* body);
    ForStatement(const SourceLocation& loc, Statement* init, Expression* condition, Statement* increment, BlockStatement* body);
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~ForStatement();
    
    // Getter for accessing the body
    BlockStatement* getBody() const { return body; }
};

// FunctionDeclaration class
class FunctionDeclaration : public Statement {
private:
    std::string name;
    std::vector<std::string> params;
    std::vector<std::string> paramTypes; // Type annotations for parameters
    std::string returnType; // Return type annotation
    BlockStatement* body;

public:
    FunctionDeclaration(const std::string& name, const std::vector<std::string>& params, BlockStatement* body);
    FunctionDeclaration(const SourceLocation& loc, const std::string& name, const std::vector<std::string>& params, BlockStatement* body);
    
    // Constructor with type annotations
    FunctionDeclaration(const SourceLocation& loc, const std::string& name, 
                       const std::vector<std::string>& params, 
                       const std::vector<std::string>& paramTypes,
                       const std::string& returnType,
                       BlockStatement* body);
    
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~FunctionDeclaration();

    // --- NUEVO ---
    void generateDeclarations(CodeGenerator& cg) override;
    llvm::Value* generateExecutableCode(CodeGenerator& cg, bool onlyFunctions = false) override;
    
    // Getters for type information
    const std::vector<std::string>& getParamTypes() const { return paramTypes; }
    const std::string& getReturnType() const { return returnType; }
    const std::vector<std::string>& getParams() const { return params; }
    const std::string& getName() const { return name; }
};

// ReturnStatement class definition
class ReturnStatement : public Statement {
private:
    Expression* value;
    
    // Helper methods for type conversion
    llvm::Value* convertValueToType(CodeGenerator& generator, llvm::Value* value, llvm::Type* targetType);
    llvm::Value* getDefaultValueForType(CodeGenerator& generator, llvm::Type* type);
    
public:
    ReturnStatement(const SourceLocation& loc, Expression* value);
    ReturnStatement(Expression* value);
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~ReturnStatement();
    
    // Getter method to access the return value expression
    Expression* getValue() const { return value; }
};

// Structure to hold variable declaration with optional type annotation
struct LetBinding {
    std::string name;
    std::string typeAnnotation; // Empty string if no type annotation
    Expression* expr;
    
    LetBinding(const std::string& n, const std::string& t, Expression* e) 
        : name(n), typeAnnotation(t), expr(e) {}
};

// LetIn class
class LetIn : public Expression {
private:
    std::vector<std::pair<std::string, Expression*>> decls; // Legacy support
    std::vector<LetBinding> typedDecls; // New typed declarations
    Expression* expr; // The "in" part is always an Expression*
    bool useTypedDecls; // Flag to indicate which declaration format to use

public:
    // Legacy constructor
    LetIn(const std::vector<std::pair<std::string, Expression*>>& decls, Expression* expr);
    LetIn(const SourceLocation& loc, const std::vector<std::pair<std::string, Expression*>>& decls, Expression* expr);
    
    // New constructor with type annotations
    LetIn(const SourceLocation& loc, const std::vector<LetBinding>& typedDecls, Expression* expr);
    
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~LetIn();
    
    // Getter for type checking
    Expression* getInExpression() const { return expr; }

private:
    void codegen_typed(CodeGenerator& generator, IContext* letContext, std::map<std::string, llvm::Value*>& oldBindings, std::vector<std::string>& declaredVars);
    void codegen_untyped(CodeGenerator& generator, IContext* letContext, std::map<std::string, llvm::Value*>& oldBindings, std::vector<std::string>& declaredVars);
};

// BlockExpression class
class BlockExpression : public Expression {
private:
    std::vector<Statement*> statements;
public:
    BlockExpression(const SourceLocation& loc, const std::vector<Statement*>& stmts);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~BlockExpression();
    const std::vector<Statement*>& getStatements() const { return statements; }
};

class IfExpression : public Expression {
public:
    Expression* condition;
    Expression* thenExpr;
    Expression* elseExpr;

    IfExpression(Expression* cond, Expression* thenExpr, Expression* elseExpr);
    IfExpression(const SourceLocation& loc, Expression* cond, Expression* thenExpr, Expression* elseExpr);
    ~IfExpression();

    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    
    // Getter methods
    Expression* getThenExpr() const { return thenExpr; }
    Expression* getElseExpr() const { return elseExpr; }
    Expression* getCondition() const { return condition; }
};

class WhileExpression : public Expression {
private:
    Expression* condition;
    BlockExpression* body;
public:
    WhileExpression(Expression* cond, BlockExpression* body);
    WhileExpression(const SourceLocation& loc, Expression* cond, BlockExpression* body);
    ~WhileExpression();
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;

    // Métodos de acceso para el parser
    Expression* getCondition() const { return condition; }
    BlockExpression* getBody() const { return body; }
};

// Forward declarations (si las necesitas)
// class FunctionDeclaration;
// class Statement;

// Structure to hold method information with return type annotation
struct MethodInfo {
    std::vector<std::string> params;
    std::string returnType; // Return type annotation
    Expression* body;
    
    MethodInfo(const std::vector<std::string>& p, const std::string& rt, Expression* b)
        : params(p), returnType(rt), body(b) {}
};

// TypeDefinition class
class TypeDefinition : public Statement {
private:
    std::string name;
    std::vector<std::string> params;
    std::vector<std::pair<std::string, std::string>> typedParams; // Parameter name and type pairs
    std::vector<std::pair<std::string, Expression*>> fields;
    std::vector<std::pair<std::string, std::pair<std::vector<std::string>, Expression*>>> methods; // Legacy
    std::vector<std::pair<std::string, MethodInfo>> typedMethods; // New with return type annotations
    std::string parentType;
    std::vector<Expression*> parentArgs;
    bool useTypedMethods; // Flag to indicate which method format to use
    bool useTypedParams; // Flag to indicate if typed parameters are used

public:
    // Legacy constructor
    TypeDefinition(const SourceLocation& loc, const std::string& name, 
                   const std::vector<std::string>& params,
                   const std::vector<std::pair<std::string, Expression*>>& fields,
                   const std::vector<std::pair<std::string, std::pair<std::vector<std::string>, Expression*>>>& methods,
                   const std::string& parentType = "Object",
                   const std::vector<Expression*>& parentArgs = {});
    
    // New constructor with typed methods
    TypeDefinition(const SourceLocation& loc, const std::string& name, 
                   const std::vector<std::string>& params,
                   const std::vector<std::pair<std::string, Expression*>>& fields,
                   const std::vector<std::pair<std::string, MethodInfo>>& typedMethods,
                   const std::string& parentType = "Object",
                   const std::vector<Expression*>& parentArgs = {});
    
    // Constructor with typed parameters and typed methods
    TypeDefinition(const SourceLocation& loc, const std::string& name, 
                   const std::vector<std::pair<std::string, std::string>>& typedParams,
                   const std::vector<std::pair<std::string, Expression*>>& fields,
                   const std::vector<std::pair<std::string, MethodInfo>>& typedMethods,
                   const std::string& parentType = "Object",
                   const std::vector<Expression*>& parentArgs = {});
    
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~TypeDefinition();
    
    const std::string& getName() const { return name; }
    const std::vector<std::string>& getParams() const { return params; }
    const std::vector<std::pair<std::string, std::string>>& getTypedParams() const { return typedParams; }
    const std::vector<std::pair<std::string, Expression*>>& getFields() const { return fields; }
    const std::vector<std::pair<std::string, std::pair<std::vector<std::string>, Expression*>>>& getMethods() const { return methods; }
    const std::vector<std::pair<std::string, MethodInfo>>& getTypedMethods() const { return typedMethods; }
    const std::string& getParentType() const { return parentType; }
    const std::vector<Expression*>& getParentArgs() const { return parentArgs; }
    bool getUseTypedMethods() const { return useTypedMethods; }
    bool getUseTypedParams() const { return useTypedParams; }
    
    // Method to register type information during code generation
    void registerType(CodeGenerator& generator);
};

// NewExpression class for object instantiation
class NewExpression : public Expression {
private:
    std::string typeName;
    std::vector<Expression*> args;

public:
    NewExpression(const SourceLocation& loc, const std::string& typeName, const std::vector<Expression*>& args);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~NewExpression();
    
    // Getter method
    const std::string& getTypeName() const { return typeName; }
};

// MemberAccess class for accessing object members
class MemberAccess : public Expression {
private:
    Expression* object;
    std::string member;

public:
    MemberAccess(const SourceLocation& loc, Expression* object, const std::string& member);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~MemberAccess();
    
    // Getter methods for accessing private members
    Expression* getObject() const { return object; }
    const std::string& getMember() const { return member; }
    
    // Method to get field pointer for assignment (returns pointer, not loaded value)
    llvm::Value* getFieldPointer(CodeGenerator& generator);
};

// SelfExpression class for 'self' keyword
class SelfExpression : public Expression {
public:
    SelfExpression(const SourceLocation& loc);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~SelfExpression();
};

// BaseExpression class for 'base()' calls
class BaseExpression : public Expression {
private:
    std::vector<Expression*> args;

public:
    BaseExpression(const SourceLocation& loc, const std::vector<Expression*>& args = {});
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~BaseExpression();
};

// MethodCall class for calling methods on objects
class MethodCall : public Expression {
private:
    Expression* object;
    std::string methodName;
    std::vector<Expression*> args;

public:
    MethodCall(const SourceLocation& loc, Expression* object, const std::string& methodName, const std::vector<Expression*>& args);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~MethodCall();
    
    // Getter for method name
    const std::string& getMethodName() const { return methodName; }
    
    // Getter for object
    Expression* getObject() const { return object; }
};

// VectorExpression class for vector literals [1,2,3] and generators [x^2 | x in range(1,10)]
class VectorExpression : public Expression {
private:
    std::vector<Expression*> elements;  // For explicit vectors
    Expression* generator;              // For generator expression (x^2)
    std::string variable;               // For generator variable (x)
    Expression* iterable;               // For generator iterable (range(1,10))
    bool isGenerator;                   // True if this is a generator vector

public:
    // Constructor for explicit vectors [1,2,3]
    VectorExpression(const SourceLocation& loc, const std::vector<Expression*>& elements);
    
    // Constructor for generator vectors [x^2 | x in range(1,10)]
    VectorExpression(const SourceLocation& loc, Expression* generator, const std::string& variable, Expression* iterable);
    
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~VectorExpression();
    
    bool getIsGenerator() const { return isGenerator; }
    const std::vector<Expression*>& getElements() const { return elements; }
    Expression* getGeneratorExpr() const { return generator; }
    const std::string& getVariable() const { return variable; }
    Expression* getIterable() const { return iterable; }
};

// VectorIndexExpression class for vector[index] access
class VectorIndexExpression : public Expression {
private:
    Expression* vector;
    Expression* index;

public:
    VectorIndexExpression(const SourceLocation& loc, Expression* vector, Expression* index);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~VectorIndexExpression();
};

// IsExpression class for type checking (obj is Type)
class IsExpression : public Expression {
private:
    Expression* object;
    std::string typeName;

public:
    IsExpression(const SourceLocation& loc, Expression* object, const std::string& typeName);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~IsExpression();
    
    // Getters
    Expression* getObject() const { return object; }
    const std::string& getTypeName() const { return typeName; }
};

// AsExpression class for downcasting (obj as Type)
class AsExpression : public Expression {
private:
    Expression* object;
    std::string typeName;

public:
    AsExpression(const SourceLocation& loc, Expression* object, const std::string& typeName);
    std::string toString() override;
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;
    ~AsExpression();
    
    // Getters
    Expression* getObject() const { return object; }
    const std::string& getTypeName() const { return typeName; }
};

class Program : public Node {
private:
    std::vector<FunctionDeclaration*> functions;
    std::vector<Statement*> statements;
    std::vector<TypeDefinition*> types;

    // Helper method for circular inheritance detection
    bool checkCircularInheritance(IContext* context);
    bool hasCircularInheritanceHelper(const std::string& typeName, IContext* context, 
                                     std::unordered_set<std::string>& visiting, 
                                     std::unordered_set<std::string>& visited);
    
    // Helper method for topological sorting of types by dependency
    std::vector<TypeDefinition*> topologicalSortTypes();

public:
    Program(const std::vector<FunctionDeclaration*>& funcs, const std::vector<Statement*>& stmts);
    Program(const SourceLocation& loc, const std::vector<FunctionDeclaration*>& funcs, const std::vector<Statement*>& stmts);
    Program(const SourceLocation& loc, const std::vector<TypeDefinition*>& types, 
            const std::vector<FunctionDeclaration*>& funcs, const std::vector<Statement*>& stmts);
    ~Program();
    void printNode(int depth) override;
    bool Validate(IContext* context) override;
    llvm::Value* codegen(CodeGenerator& generator) override;

    // --- NUEVO ---
    void generateDeclarations(CodeGenerator& cg) override;
    llvm::Value* generateExecutableCode(CodeGenerator& cg, bool onlyFunctions = false) override;
};


#endif // TREE_HPP
