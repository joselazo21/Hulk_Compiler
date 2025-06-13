#include <cstdio>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include "SintacticoBison.tab.hpp"
#include "tree.hpp" 
#include "code_generator.hpp"
#include "error_handler.hpp"
#include <llvm/IR/LLVMContext.h>

// External declarations from custom lexer/bison
extern int yyparse(void);
extern int yylineno;
extern int yycolumn;
extern char* yytext;
extern Node* root;
extern void yyerror(const char *s);
extern void set_input_from_file(FILE* file);

// Global variable for current filename (used by error handler)
std::string current_filename;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Uso: " << argv[0] << " <archivo.hulk>" << std::endl;
        return 1;
    }
    
    std::string inputFile = argv[1];
    current_filename = inputFile;
    
    // Open input file
    FILE *input_file = fopen(inputFile.c_str(), "r");
    if (!input_file) {
        std::cerr << "Error: No se pudo abrir el archivo '" << inputFile << "'" << std::endl;
        return 1;
    }
    
    // Reset parser state and set input
    set_input_from_file(input_file);
    root = nullptr;
    ErrorHandler::getInstance().reset();
    
    std::cout << "Compilando archivo: " << inputFile << std::endl;
    std::cout << "\n--- Análisis Léxico y Sintáctico ---" << std::endl;
    
    // Parse the input
    int parse_result = yyparse();
    fclose(input_file);
    
    // Check for parsing errors
    if (parse_result != 0 || ErrorHandler::getInstance().hadErrors()) {
        std::cerr << "Error: Fallo en el análisis sintáctico" << std::endl;
        return 1;
    }
    
    if (!root) {
        std::cerr << "Error: No se generó el AST" << std::endl;
        return 1;
    }
    
    std::cout << "✅ Análisis sintáctico completado exitosamente" << std::endl;
    
    // Semantic analysis
    std::cout << "\n--- Análisis Semántico ---" << std::endl;
    llvm::LLVMContext llvmCtx;
    Context globalContext(nullptr, &llvmCtx);
    
    if (!root->Validate(&globalContext) || ErrorHandler::getInstance().hadErrors()) {
        std::cerr << "Error: Fallo en el análisis semántico" << std::endl;
        return 1;
    }
    
    std::cout << "✅ Análisis semántico completado exitosamente" << std::endl;
    
    // Generate AST output file and display in console
    std::cout << "\n--- Generando AST ---" << std::endl;
    
    // First, print AST to console
    std::cout << "\n--- Árbol de Sintaxis Abstracta (AST) ---" << std::endl;
    root->printNode(0);
    std::cout << "--- Fin del AST ---\n" << std::endl;
    
    // Also save AST to file
    std::ofstream astFile("ast.txt");
    if (astFile.is_open()) {
        // Redirect cout to file temporarily
        std::streambuf* orig = std::cout.rdbuf();
        std::cout.rdbuf(astFile.rdbuf());
        root->printNode(0);
        std::cout.rdbuf(orig);
        astFile.close();
        std::cout << "✅ AST guardado en ast.txt" << std::endl;
    }
    
    // Code generation
    std::cout << "\n--- Generación de Código LLVM ---" << std::endl;
    {
        CodeGenerator generator(&llvmCtx, &globalContext);
        if (!generator.generateCode(root)) {
            std::cerr << "Error: Fallo durante la generación de código" << std::endl;
            return 1;
        }
        
        std::cout << "✅ Código LLVM generado exitosamente" << std::endl;
        
        // Extract base filename without extension
        std::string baseFilename = inputFile;
        size_t lastDot = baseFilename.find_last_of(".");
        if (lastDot != std::string::npos) {
            baseFilename = baseFilename.substr(0, lastDot);
        }
        
        // Save LLVM IR to file
        std::string outputFile = baseFilename + ".ll";
        if (generator.saveModuleToFile(outputFile)) {
            std::cout << "✅ Código LLVM guardado en: " << outputFile << std::endl;
        } else {
            std::cerr << "Error: No se pudo guardar el archivo LLVM IR" << std::endl;
            return 1;
        }
    }
    
    std::cout << "\n🎉 Compilación completada exitosamente!" << std::endl;
    return 0;
}