#ifndef ERROR_HANDLER_HPP
#define ERROR_HANDLER_HPP

#include <string>
#include <vector>
#include <iostream>

struct SourceLocation {
    int line;
    int column;
    std::string filename;

    SourceLocation(int l = 0, int c = 0, std::string f = "entrada.txt") 
        : line(l), column(c), filename(f) {}
};

class ErrorHandler {
public:
    static ErrorHandler& getInstance() {
        static ErrorHandler instance;
        return instance;
    }

    void semanticError(const std::string& message, const SourceLocation& loc) {
        std::cerr << loc.filename << ":" << loc.line << ":" << loc.column 
                  << ": error semántico: " << message << std::endl;
        hasErrors = true;
    }

    void syntaxError(const std::string& message, const SourceLocation& loc) {
        std::cerr << loc.filename << ":" << loc.line << ":" << loc.column 
                  << ": error de sintaxis: " << message << std::endl;
        hasErrors = true;
    }

    void codegenError(const std::string& message, const SourceLocation& loc) {
        std::cerr << loc.filename << ":" << loc.line << ":" << loc.column 
                  << ": error de generación de código: " << message << std::endl;
        hasErrors = true;
    }

    bool hadErrors() const { return hasErrors; }
    void reset() { hasErrors = false; }

private:
    ErrorHandler() : hasErrors(false) {}
    bool hasErrors;
};

#define SEMANTIC_ERROR(msg, loc) ErrorHandler::getInstance().semanticError(msg, loc)
#define SYNTAX_ERROR(msg, loc) ErrorHandler::getInstance().syntaxError(msg, loc)
#define CODEGEN_ERROR(msg, loc) ErrorHandler::getInstance().codegenError(msg, loc)

#endif // ERROR_HANDLER_HPP