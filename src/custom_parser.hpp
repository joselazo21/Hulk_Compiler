#ifndef CUSTOM_PARSER_HPP
#define CUSTOM_PARSER_HPP

#include "custom_lexer.hpp"
#include "tree.hpp"
#include <vector>
#include <memory>

class CustomParser {
private:
    CustomLexer* lexer;
    CustomToken currentToken;
    std::vector<std::string> errors;
    
public:
    CustomParser(CustomLexer* lexer);
    
    // Main parsing method
    Node* parse();
    
    // Error handling
    const std::vector<std::string>& getErrors() const { return errors; }
    bool hasErrors() const { return !errors.empty(); }
    void clearErrors() { errors.clear(); }
    
private:
    // Token management
    void advance();
    bool match(CustomTokenType type);
    bool check(CustomTokenType type);
    CustomToken consume(CustomTokenType type, const std::string& message);
    
    // Error handling
    void addError(const std::string& message);
    void synchronize();
    
    // Grammar rules
    Program* parseProgram();
    std::vector<TypeDefinition*> parseTypeList();
    TypeDefinition* parseTypeDefinition();
    std::vector<FunctionDeclaration*> parseFunctionList();
    FunctionDeclaration* parseFunctionDeclaration();
    std::vector<Statement*> parseStatementList();
    Statement* parseStatement();
    Expression* parseExpression();
    Expression* parseOrExpression();
    Expression* parseAndExpression();
    Expression* parseEqualityExpression();
    Expression* parseRelationalExpression();
    Expression* parseAdditiveExpression();
    Expression* parseMultiplicativeExpression();
    Expression* parsePowerExpression();
    Expression* parseUnaryExpression();
    Expression* parsePostfixExpression();
    Expression* parsePrimaryExpression();
    
    // Helper methods
    std::vector<std::string> parseParameterList();
    std::vector<std::pair<std::string, std::string>> parseTypedParameterList();
    std::vector<Expression*> parseExpressionList();
    std::vector<LetBinding> parseBindingList();
    BlockStatement* parseBlockStatement();
    BlockExpression* parseBlockExpression();
    
    // Utility methods
    SourceLocation getCurrentLocation();
    bool isAtEnd();
};

#endif // CUSTOM_PARSER_HPP