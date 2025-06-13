#ifndef CUSTOM_LEXER_HPP
#define CUSTOM_LEXER_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// Token types matching the Bison parser expectations
enum class CustomTokenType {
    // Keywords
    LET, PRINT, FUNCTION, IF, ELSE, ELIF, WHILE, FOR, IN,
    TYPE, NEW, INHERITS, SELF, BASE, IS,
    
    // Operators
    ASSIGN, ASSIGN_OP, EQ, NEQ, LE, GE, AND, OR, ARROW,
    CONCAT, CONCAT_WS, MOD,
    
    // Single character tokens (matching Bison %token declarations)
    PLUS, MINUS, MULT, DIV, POW,
    LT, GT, COLON, DOT,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    COMMA, SEMICOLON, PIPE,
    
    // Literals
    NUMBER, ID, STRING,
    
    // Special
    END_OF_FILE, ERROR
};

// Token structure compatible with Bison
struct CustomToken {
    CustomTokenType type;
    std::string lexeme;
    int line;
    int column;
    double numericValue; // For NUMBER tokens
    
    CustomToken(CustomTokenType t = CustomTokenType::ERROR, const std::string& l = "", 
               int ln = 0, int col = 0, double val = 0.0);
    
    std::string toString() const;
    
    // Convert to Bison token type (int)
    int toBisonToken() const;
};

// Custom lexer class
class CustomLexer {
private:
    std::string input;
    size_t position;
    int line;
    int column;
    char currentChar;
    
    // Symbol tables
    std::unordered_map<std::string, CustomTokenType> keywords;
    std::unordered_map<std::string, CustomTokenType> operators;
    std::unordered_set<char> singleCharOperators;
    std::unordered_set<char> delimiters;
    
    // Error handling
    std::vector<std::string> errors;
    
    // Current token for Bison interface
    CustomToken currentToken;
    
public:
    CustomLexer(const std::string& source);
    
    // Bison interface methods
    int yylex();  // Main lexer function for Bison
    void setInput(const std::string& source);
    void reset();
    
    // Token access
    const CustomToken& getCurrentToken() const { return currentToken; }
    std::string getCurrentLexeme() const { return currentToken.lexeme; }
    double getCurrentNumericValue() const { return currentToken.numericValue; }
    int getCurrentLine() const { return currentToken.line; }
    int getCurrentColumn() const { return currentToken.column; }
    
    // Error handling
    const std::vector<std::string>& getErrors() const { return errors; }
    bool hasErrors() const { return !errors.empty(); }
    void clearErrors() { errors.clear(); }
    
    // Utility methods
    std::vector<CustomToken> tokenize();
    void printTokens(const std::vector<CustomToken>& tokens) const;
    
    // Token processing methods (public for parser access)
    CustomToken getNextToken();
    
private:
    // Initialization
    void initializeSymbolTables();
    void initializeKeywords();
    void initializeOperators();
    void initializeDelimiters();
    
    // Character handling
    void advance();
    char peek(int offset = 1) const;
    void skipWhitespaceAndNewlines();
    
    // Token processing methods (private)
    CustomToken processNumber();
    CustomToken processFloatingPoint(const std::string& integerPart);
    CustomToken processIdentifier();
    CustomToken processString();
    CustomToken processOperator();
    
    // Helper methods
    bool isValidIdentifierStart(char c) const;
    bool isValidIdentifierChar(char c) const;
    bool isOperatorStart(char c) const;
    std::string getOperatorSequence();
    
    // Error handling
    void addError(const std::string& message);
    CustomToken createErrorToken(const std::string& message);
};

// Global lexer instance for Bison interface
extern CustomLexer* globalLexer;

// Bison interface functions
extern "C" {
    int yylex();
    void yyerror(const char* s);
}

// Global variables for Bison compatibility
extern int yylineno;
extern int yycolumn;
extern char* yytext;
extern double yylval_dval;
extern char* yylval_sval;

#endif // CUSTOM_LEXER_HPP