#include "custom_parser.hpp"
#include "error_handler.hpp"
#include <iostream>

CustomParser::CustomParser(CustomLexer* lexer) : lexer(lexer) {
    advance(); // Get the first token
}

Node* CustomParser::parse() {
    try {
        return parseProgram();
    } catch (const std::exception& e) {
        addError("Parse error: " + std::string(e.what()));
        return nullptr;
    }
}

void CustomParser::advance() {
    currentToken = lexer->getNextToken();
}

bool CustomParser::match(CustomTokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool CustomParser::check(CustomTokenType type) {
    if (isAtEnd()) return false;
    return currentToken.type == type;
}

CustomToken CustomParser::consume(CustomTokenType type, const std::string& message) {
    if (check(type)) {
        CustomToken token = currentToken;
        advance();
        return token;
    }
    
    addError(message + " at line " + std::to_string(currentToken.line) + 
             ", column " + std::to_string(currentToken.column));
    return currentToken;
}

void CustomParser::addError(const std::string& message) {
    errors.push_back(message);
    SYNTAX_ERROR(message, getCurrentLocation());
}

void CustomParser::synchronize() {
    advance();
    
    while (!isAtEnd()) {
        if (currentToken.type == CustomTokenType::SEMICOLON) {
            advance();
            return;
        }
        
        switch (currentToken.type) {
            case CustomTokenType::TYPE:
            case CustomTokenType::FUNCTION:
            case CustomTokenType::LET:
            case CustomTokenType::IF:
            case CustomTokenType::WHILE:
            case CustomTokenType::FOR:
            case CustomTokenType::PRINT:
                return;
            default:
                break;
        }
        
        advance();
    }
}

Program* CustomParser::parseProgram() {
    SourceLocation loc = getCurrentLocation();
    
    std::vector<TypeDefinition*> types = parseTypeList();
    std::vector<FunctionDeclaration*> functions = parseFunctionList();
    std::vector<Statement*> statements = parseStatementList();
    
    return new Program(loc, types, functions, statements);
}

std::vector<TypeDefinition*> CustomParser::parseTypeList() {
    std::vector<TypeDefinition*> types;
    
    while (check(CustomTokenType::TYPE)) {
        TypeDefinition* typeDef = parseTypeDefinition();
        if (typeDef) {
            types.push_back(typeDef);
        }
    }
    
    return types;
}

TypeDefinition* CustomParser::parseTypeDefinition() {
    SourceLocation loc = getCurrentLocation();
    
    consume(CustomTokenType::TYPE, "Expected 'type'");
    CustomToken nameToken = consume(CustomTokenType::ID, "Expected type name");
    
    std::vector<std::string> params;
    if (match(CustomTokenType::LPAREN)) {
        auto typedParams = parseTypedParameterList();
        for (const auto& param : typedParams) {
            params.push_back(param.first);
        }
        consume(CustomTokenType::RPAREN, "Expected ')' after type parameters");
    }
    
    std::string parentType = "Object";
    std::vector<Expression*> parentArgs;
    
    if (match(CustomTokenType::INHERITS)) {
        CustomToken parentToken = consume(CustomTokenType::ID, "Expected parent type name");
        parentType = parentToken.lexeme;
        
        if (match(CustomTokenType::LPAREN)) {
            parentArgs = parseExpressionList();
            consume(CustomTokenType::RPAREN, "Expected ')' after parent arguments");
        }
    }
    
    consume(CustomTokenType::LBRACE, "Expected '{' to start type body");
    
    std::vector<std::pair<std::string, Expression*>> fields;
    std::vector<std::pair<std::string, MethodInfo>> typedMethods;
    
    while (!check(CustomTokenType::RBRACE) && !isAtEnd()) {
        CustomToken memberName = consume(CustomTokenType::ID, "Expected member name");
        
        if (match(CustomTokenType::ASSIGN)) {
            // Field
            Expression* value = parseExpression();
            fields.push_back(std::make_pair(memberName.lexeme, value));
            consume(CustomTokenType::SEMICOLON, "Expected ';' after field");
        } else if (match(CustomTokenType::COLON)) {
            // Typed field
            consume(CustomTokenType::ID, "Expected field type");
            consume(CustomTokenType::ASSIGN, "Expected '=' after field type");
            Expression* value = parseExpression();
            fields.push_back(std::make_pair(memberName.lexeme, value));
            consume(CustomTokenType::SEMICOLON, "Expected ';' after typed field");
        } else if (match(CustomTokenType::LPAREN)) {
            // Method
            std::vector<std::string> methodParams = parseParameterList();
            consume(CustomTokenType::RPAREN, "Expected ')' after method parameters");
            
            std::string returnType = "Number"; // Default return type
            std::cout << "[DEBUG] Parser: Before checking for colon, current token: " << currentToken.toString() << std::endl;
            if (match(CustomTokenType::COLON)) {
                std::cout << "[DEBUG] Parser: Found colon, consuming return type token" << std::endl;
                CustomToken typeToken = consume(CustomTokenType::ID, "Expected return type");
                returnType = typeToken.lexeme;
                std::cout << "[DEBUG] Parser: Set return type to: '" << returnType << "'" << std::endl;
            } else {
                std::cout << "[DEBUG] Parser: No colon found, using default return type: '" << returnType << "'" << std::endl;
            }
            
            consume(CustomTokenType::ARROW, "Expected '=>' before method body");
            Expression* body = parseExpression();
            
            MethodInfo methodInfo(methodParams, returnType, body);
            typedMethods.push_back(std::make_pair(memberName.lexeme, methodInfo));
            consume(CustomTokenType::SEMICOLON, "Expected ';' after method");
        } else {
            addError("Expected field assignment or method definition");
            synchronize();
        }
    }
    
    consume(CustomTokenType::RBRACE, "Expected '}' to end type body");
    
    return new TypeDefinition(loc, nameToken.lexeme, params, fields, typedMethods, parentType, parentArgs);
}

std::vector<FunctionDeclaration*> CustomParser::parseFunctionList() {
    std::vector<FunctionDeclaration*> functions;
    
    while (check(CustomTokenType::FUNCTION)) {
        FunctionDeclaration* func = parseFunctionDeclaration();
        if (func) {
            functions.push_back(func);
        }
    }
    
    return functions;
}

FunctionDeclaration* CustomParser::parseFunctionDeclaration() {
    SourceLocation loc = getCurrentLocation();
    
    consume(CustomTokenType::FUNCTION, "Expected 'function'");
    CustomToken nameToken = consume(CustomTokenType::ID, "Expected function name");
    
    consume(CustomTokenType::LPAREN, "Expected '(' after function name");
    auto typedParams = parseTypedParameterList();
    consume(CustomTokenType::RPAREN, "Expected ')' after parameters");
    
    std::string returnType = "Number"; // Default return type
    if (match(CustomTokenType::COLON)) {
        CustomToken typeToken = consume(CustomTokenType::ID, "Expected return type");
        returnType = typeToken.lexeme;
    }
    
    std::vector<std::string> params;
    std::vector<std::string> paramTypes;
    for (const auto& param : typedParams) {
        params.push_back(param.first);
        paramTypes.push_back(param.second.empty() ? "Number" : param.second);
    }
    
    if (match(CustomTokenType::ARROW)) {
        // Expression function
        Expression* expr = parseExpression();
        consume(CustomTokenType::SEMICOLON, "Expected ';' after function expression");
        
        std::vector<Statement*> statements;
        statements.push_back(new ReturnStatement(loc, expr));
        BlockStatement* body = new BlockStatement(loc, statements);
        
        return new FunctionDeclaration(loc, nameToken.lexeme, params, paramTypes, returnType, body);
    } else if (match(CustomTokenType::LBRACE)) {
        // Block function
        std::vector<Statement*> statements = parseStatementList();
        consume(CustomTokenType::RBRACE, "Expected '}' after function body");
        
        BlockStatement* body = new BlockStatement(loc, statements);
        return new FunctionDeclaration(loc, nameToken.lexeme, params, paramTypes, returnType, body);
    } else {
        addError("Expected '=>' or '{' after function signature");
        return nullptr;
    }
}

std::vector<Statement*> CustomParser::parseStatementList() {
    std::vector<Statement*> statements;
    
    while (!check(CustomTokenType::RBRACE) && !check(CustomTokenType::END_OF_FILE) && !isAtEnd()) {
        Statement* stmt = parseStatement();
        if (stmt) {
            statements.push_back(stmt);
        } else {
            synchronize();
        }
    }
    
    return statements;
}

Statement* CustomParser::parseStatement() {
    SourceLocation loc = getCurrentLocation();
    
    if (check(CustomTokenType::ID)) {
        // Could be assignment or expression statement
        CustomToken idToken = currentToken;
        advance();
        
        if (match(CustomTokenType::ASSIGN)) {
            Expression* expr = parseExpression();
            consume(CustomTokenType::SEMICOLON, "Expected ';' after assignment");
            return new Assignment(loc, idToken.lexeme, expr);
        } else {
            // Put the token back and parse as expression statement
            // This is a simplified approach - in a real parser you'd use lookahead
            // For now, we'll create a variable expression and check if it's followed by '('
            Expression* expr;
            if (check(CustomTokenType::LPAREN)) {
                // Function call
                std::vector<Expression*> args = parseExpressionList();
                consume(CustomTokenType::RPAREN, "Expected ')' after function arguments");
                expr = new FunctionCall(loc, idToken.lexeme, args);
            } else {
                expr = new Variable(loc, idToken.lexeme);
            }
            consume(CustomTokenType::SEMICOLON, "Expected ';' after expression");
            return new ExpressionStatement(loc, expr);
        }
    } else if (check(CustomTokenType::WHILE)) {
        advance();
        consume(CustomTokenType::LPAREN, "Expected '(' after 'while'");
        Expression* condition = parseExpression();
        consume(CustomTokenType::RPAREN, "Expected ')' after while condition");
        consume(CustomTokenType::LBRACE, "Expected '{' after while condition");
        std::vector<Statement*> statements = parseStatementList();
        consume(CustomTokenType::RBRACE, "Expected '}' after while body");
        
        BlockStatement* body = new BlockStatement(loc, statements);
        return new WhileStatement(loc, condition, body);
    } else {
        // Expression statement
        Expression* expr = parseExpression();
        consume(CustomTokenType::SEMICOLON, "Expected ';' after expression");
        return new ExpressionStatement(loc, expr);
    }
}

Expression* CustomParser::parseExpression() {
    return parseOrExpression();
}

Expression* CustomParser::parseOrExpression() {
    Expression* expr = parseAndExpression();
    
    while (match(CustomTokenType::OR)) {
        SourceLocation loc = getCurrentLocation();
        Expression* right = parseAndExpression();
        expr = new BinaryOperation(loc, expr, right, "||");
    }
    
    return expr;
}

Expression* CustomParser::parseAndExpression() {
    Expression* expr = parseEqualityExpression();
    
    while (match(CustomTokenType::AND)) {
        SourceLocation loc = getCurrentLocation();
        Expression* right = parseEqualityExpression();
        expr = new BinaryOperation(loc, expr, right, "&&");
    }
    
    return expr;
}

Expression* CustomParser::parseEqualityExpression() {
    Expression* expr = parseRelationalExpression();
    
    while (check(CustomTokenType::EQ) || check(CustomTokenType::NEQ)) {
        SourceLocation loc = getCurrentLocation();
        std::string op = currentToken.lexeme;
        advance();
        Expression* right = parseRelationalExpression();
        expr = new BinaryOperation(loc, expr, right, op);
    }
    
    return expr;
}

Expression* CustomParser::parseRelationalExpression() {
    Expression* expr = parseAdditiveExpression();
    
    while (check(CustomTokenType::LT) || check(CustomTokenType::GT) || 
           check(CustomTokenType::LE) || check(CustomTokenType::GE) ||
           check(CustomTokenType::IS)) {
        SourceLocation loc = getCurrentLocation();
        
        if (check(CustomTokenType::IS)) {
            advance(); // consume 'is'
            CustomToken typeToken = consume(CustomTokenType::ID, "Expected type name after 'is'");
            expr = new IsExpression(loc, expr, typeToken.lexeme);
        } else {
            std::string op = currentToken.lexeme;
            advance();
            Expression* right = parseAdditiveExpression();
            expr = new BinaryOperation(loc, expr, right, op);
        }
    }
    
    return expr;
}

Expression* CustomParser::parseAdditiveExpression() {
    Expression* expr = parseMultiplicativeExpression();
    
    while (check(CustomTokenType::PLUS) || check(CustomTokenType::MINUS) ||
           check(CustomTokenType::CONCAT) || check(CustomTokenType::CONCAT_WS)) {
        SourceLocation loc = getCurrentLocation();
        std::string op = currentToken.lexeme;
        advance();
        Expression* right = parseMultiplicativeExpression();
        expr = new BinaryOperation(loc, expr, right, op);
    }
    
    return expr;
}

Expression* CustomParser::parseMultiplicativeExpression() {
    Expression* expr = parsePowerExpression();
    
    while (check(CustomTokenType::MULT) || check(CustomTokenType::DIV) || check(CustomTokenType::MOD)) {
        SourceLocation loc = getCurrentLocation();
        std::string op = currentToken.lexeme;
        advance();
        Expression* right = parsePowerExpression();
        expr = new BinaryOperation(loc, expr, right, op);
    }
    
    return expr;
}

Expression* CustomParser::parsePowerExpression() {
    Expression* expr = parseUnaryExpression();
    
    if (match(CustomTokenType::POW)) {
        SourceLocation loc = getCurrentLocation();
        Expression* right = parsePowerExpression(); // Right associative
        expr = new BinaryOperation(loc, expr, right, "^");
    }
    
    return expr;
}

Expression* CustomParser::parseUnaryExpression() {
    return parsePostfixExpression();
}

Expression* CustomParser::parsePostfixExpression() {
    Expression* expr = parsePrimaryExpression();
    
    while (true) {
        if (match(CustomTokenType::DOT)) {
            CustomToken memberToken = consume(CustomTokenType::ID, "Expected member name after '.'");
            if (match(CustomTokenType::LPAREN)) {
                // Method call
                std::vector<Expression*> args = parseExpressionList();
                consume(CustomTokenType::RPAREN, "Expected ')' after method arguments");
                expr = new MethodCall(getCurrentLocation(), expr, memberToken.lexeme, args);
            } else if (match(CustomTokenType::ASSIGN)) {
                // Member assignment
                Expression* value = parseExpression();
                MemberAccess* memberAccess = new MemberAccess(getCurrentLocation(), expr, memberToken.lexeme);
                expr = new AssignmentExpression(getCurrentLocation(), memberAccess, value);
            } else {
                // Member access
                expr = new MemberAccess(getCurrentLocation(), expr, memberToken.lexeme);
            }
        } else if (match(CustomTokenType::LBRACKET)) {
            Expression* index = parseExpression();
            consume(CustomTokenType::RBRACKET, "Expected ']' after array index");
            expr = new VectorIndexExpression(getCurrentLocation(), expr, index);
        } else {
            break;
        }
    }
    
    return expr;
}

Expression* CustomParser::parsePrimaryExpression() {
    SourceLocation loc = getCurrentLocation();
    
    if (check(CustomTokenType::NUMBER)) {
        double value = currentToken.numericValue;
        advance();
        return new Number(loc, value);
    }
    
    if (check(CustomTokenType::STRING)) {
        std::string value = currentToken.lexeme;
        advance();
        return new StringLiteral(loc, value);
    }
    
    if (check(CustomTokenType::ID)) {
        std::string name = currentToken.lexeme;
        advance();
        if (match(CustomTokenType::LPAREN)) {
            // Function call
            std::vector<Expression*> args = parseExpressionList();
            consume(CustomTokenType::RPAREN, "Expected ')' after function arguments");
            return new FunctionCall(loc, name, args);
        } else if (match(CustomTokenType::ASSIGN)) {
            // Assignment expression
            Expression* value = parseExpression();
            return new AssignmentExpression(loc, name, value);
        } else {
            // Variable
            return new Variable(loc, name);
        }
    }
    
    if (match(CustomTokenType::LPAREN)) {
        Expression* expr = parseExpression();
        consume(CustomTokenType::RPAREN, "Expected ')' after expression");
        return expr;
    }
    
    if (match(CustomTokenType::LBRACKET)) {
        std::vector<Expression*> elements = parseExpressionList();
        consume(CustomTokenType::RBRACKET, "Expected ']' after vector elements");
        return new VectorExpression(loc, elements);
    }
    
    if (match(CustomTokenType::LBRACE)) {
        std::vector<Statement*> statements = parseStatementList();
        consume(CustomTokenType::RBRACE, "Expected '}' after block");
        return new BlockExpression(loc, statements);
    }
    
    if (match(CustomTokenType::LET)) {
        std::vector<LetBinding> bindings = parseBindingList();
        consume(CustomTokenType::IN, "Expected 'in' after let bindings");
        Expression* expr = parseExpression();
        return new LetIn(loc, bindings, expr);
    }
    
    if (match(CustomTokenType::IF)) {
        consume(CustomTokenType::LPAREN, "Expected '(' after 'if'");
        Expression* condition = parseExpression();
        consume(CustomTokenType::RPAREN, "Expected ')' after if condition");
        Expression* thenExpr = parseExpression();
        
        Expression* elseExpr = nullptr;
        
        // Handle elif chains
        while (match(CustomTokenType::ELIF)) {
            consume(CustomTokenType::LPAREN, "Expected '(' after 'elif'");
            Expression* elifCondition = parseExpression();
            consume(CustomTokenType::RPAREN, "Expected ')' after elif condition");
            Expression* elifThenExpr = parseExpression();
            
            // Create nested if-else structure for elif
            if (elseExpr == nullptr) {
                elseExpr = new IfExpression(getCurrentLocation(), elifCondition, elifThenExpr, nullptr);
            } else {
                // Find the deepest else clause and replace it
                IfExpression* current = dynamic_cast<IfExpression*>(elseExpr);
                while (current && current->elseExpr && dynamic_cast<IfExpression*>(current->elseExpr)) {
                    current = dynamic_cast<IfExpression*>(current->elseExpr);
                }
                if (current) {
                    current->elseExpr = new IfExpression(getCurrentLocation(), elifCondition, elifThenExpr, nullptr);
                }
            }
        }
        
        // Handle final else clause
        if (match(CustomTokenType::ELSE)) {
            Expression* finalElseExpr = parseExpression();
            if (elseExpr == nullptr) {
                elseExpr = finalElseExpr;
            } else {
                // Find the deepest else clause and replace it
                IfExpression* current = dynamic_cast<IfExpression*>(elseExpr);
                while (current && current->elseExpr && dynamic_cast<IfExpression*>(current->elseExpr)) {
                    current = dynamic_cast<IfExpression*>(current->elseExpr);
                }
                if (current) {
                    current->elseExpr = finalElseExpr;
                }
            }
        }
        
        return new IfExpression(loc, condition, thenExpr, elseExpr);
    }
    
    if (match(CustomTokenType::WHILE)) {
        consume(CustomTokenType::LPAREN, "Expected '(' after 'while'");
        Expression* condition = parseExpression();
        consume(CustomTokenType::RPAREN, "Expected ')' after while condition");
        BlockExpression* body = parseBlockExpression();
        return new WhileExpression(loc, condition, body);
    }
    
    if (match(CustomTokenType::PRINT)) {
        consume(CustomTokenType::LPAREN, "Expected '(' after 'print'");
        Expression* expr = parseExpression();
        consume(CustomTokenType::RPAREN, "Expected ')' after print argument");
        return new Print(loc, expr);
    }
    
    if (match(CustomTokenType::NEW)) {
        CustomToken typeToken = consume(CustomTokenType::ID, "Expected type name after 'new'");
        consume(CustomTokenType::LPAREN, "Expected '(' after type name");
        std::vector<Expression*> args = parseExpressionList();
        consume(CustomTokenType::RPAREN, "Expected ')' after constructor arguments");
        return new NewExpression(loc, typeToken.lexeme, args);
    }
    
    if (match(CustomTokenType::SELF)) {
        return new SelfExpression(loc);
    }
    
    if (match(CustomTokenType::BASE)) {
        consume(CustomTokenType::LPAREN, "Expected '(' after 'base'");
        std::vector<Expression*> args = parseExpressionList();
        consume(CustomTokenType::RPAREN, "Expected ')' after base arguments");
        return new BaseExpression(loc, args);
    }
    
    addError("Unexpected token: " + currentToken.toString());
    return nullptr;
}

std::vector<std::string> CustomParser::parseParameterList() {
    std::vector<std::string> params;
    
    if (!check(CustomTokenType::RPAREN)) {
        do {
            CustomToken paramToken = consume(CustomTokenType::ID, "Expected parameter name");
            params.push_back(paramToken.lexeme);
        } while (match(CustomTokenType::COMMA));
    }
    
    return params;
}

std::vector<std::pair<std::string, std::string>> CustomParser::parseTypedParameterList() {
    std::vector<std::pair<std::string, std::string>> params;
    
    if (!check(CustomTokenType::RPAREN)) {
        do {
            CustomToken paramToken = consume(CustomTokenType::ID, "Expected parameter name");
            std::string paramType = "";
            
            if (match(CustomTokenType::COLON)) {
                CustomToken typeToken = consume(CustomTokenType::ID, "Expected parameter type");
                paramType = typeToken.lexeme;
            }
            
            params.push_back(std::make_pair(paramToken.lexeme, paramType));
        } while (match(CustomTokenType::COMMA));
    }
    
    return params;
}

std::vector<Expression*> CustomParser::parseExpressionList() {
    std::vector<Expression*> expressions;
    
    if (!check(CustomTokenType::RPAREN) && !check(CustomTokenType::RBRACKET)) {
        do {
            Expression* expr = parseExpression();
            if (expr) {
                expressions.push_back(expr);
            }
        } while (match(CustomTokenType::COMMA));
    }
    
    return expressions;
}

std::vector<LetBinding> CustomParser::parseBindingList() {
    std::vector<LetBinding> bindings;
    
    do {
        // Handle 'let' keyword for subsequent bindings after comma
        if (bindings.size() > 0) {
            consume(CustomTokenType::LET, "Expected 'let' keyword for binding");
        }
        
        CustomToken nameToken = consume(CustomTokenType::ID, "Expected variable name");
        std::string typeAnnotation = "";
        
        if (match(CustomTokenType::COLON)) {
            CustomToken typeToken = consume(CustomTokenType::ID, "Expected type annotation");
            typeAnnotation = typeToken.lexeme;
        }
        
        consume(CustomTokenType::ASSIGN, "Expected '=' after variable name");
        Expression* expr = parseExpression();
        
        bindings.push_back(LetBinding(nameToken.lexeme, typeAnnotation, expr));
    } while (match(CustomTokenType::COMMA));
    
    return bindings;
}

BlockStatement* CustomParser::parseBlockStatement() {
    SourceLocation loc = getCurrentLocation();
    consume(CustomTokenType::LBRACE, "Expected '{'");
    std::vector<Statement*> statements = parseStatementList();
    consume(CustomTokenType::RBRACE, "Expected '}'");
    return new BlockStatement(loc, statements);
}

BlockExpression* CustomParser::parseBlockExpression() {
    SourceLocation loc = getCurrentLocation();
    consume(CustomTokenType::LBRACE, "Expected '{'");
    std::vector<Statement*> statements = parseStatementList();
    consume(CustomTokenType::RBRACE, "Expected '}'");
    return new BlockExpression(loc, statements);
}

SourceLocation CustomParser::getCurrentLocation() {
    return SourceLocation(currentToken.line, currentToken.column);
}

bool CustomParser::isAtEnd() {
    return currentToken.type == CustomTokenType::END_OF_FILE;
}