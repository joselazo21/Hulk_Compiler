#include "../include/code_generator.hpp"
#include "../include/tree.hpp"
#include "../include/type_system.hpp"
#include "print.h"
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <vector>
#include <string>
#include <iostream>
#include <map>

// === Helpers para nombres únicos de global y funciones de iterador ===
std::string getIteratorGlobalName(const std::string& iterName) {
    // "__iter_x" -> "__current_range_x"
    if (iterName.rfind("__iter_", 0) == 0) {
        return "__current_range_" + iterName.substr(7);
    }
    return "__current_range_" + iterName;
}

std::string getIterNextFunctionName(const std::string& iterName) {
    return iterName + ".next";
}

std::string getIterCurrentFunctionName(const std::string& iterName) {
    return iterName + ".current";
}

// Helper para obtener siempre el mismo tipo de struct.range*
llvm::StructType* getRangeStructType(llvm::LLVMContext& ctx) {
    llvm::StructType* rangeStruct = llvm::StructType::getTypeByName(ctx, "struct.range");
    if (!rangeStruct) {
        std::vector<llvm::Type*> members = {
            llvm::Type::getInt32Ty(ctx), // current
            llvm::Type::getInt32Ty(ctx)  // end
        };
        rangeStruct = llvm::StructType::create(ctx, members, "struct.range");
    }
    return rangeStruct;
}

llvm::PointerType* getRangePtrType(llvm::LLVMContext& ctx) {
    return getRangeStructType(ctx)->getPointerTo();
}

CodeGenerator::CodeGenerator(llvm::LLVMContext* extContext, Context* extContextObject) 
    : TheContext(extContext),
      TheModule(new llvm::Module("my_module", *TheContext)),
      Builder(new llvm::IRBuilder<>(*TheContext)),
      contextObject(extContextObject),
      currentLetInVarName("") {
    
    initializeModule();
}

CodeGenerator::~CodeGenerator() {
    std::cout << "[LOG] CodeGenerator::~CodeGenerator() START\n";

    delete Builder; // Builder should be deleted before TheModule if it holds references
    Builder = nullptr;

    delete TheModule; // TheModule's destructor will clean up all its contents (Functions, Globals, etc.)
                      // This operation REQUIRES TheContext (llvmCtx from main) to be alive.
    TheModule = nullptr;

    std::cout << "[LOG] CodeGenerator::~CodeGenerator(), cleaning contextStack, current size=" << contextStack.size() << "\n";
    while (!contextStack.empty()) {
        IContext* toDelete = contextStack.back();
        contextStack.pop_back();

        if (toDelete != contextObject) { // Ensure we don't delete the global context instance from main
            std::cout << "[LOG] CodeGenerator::~CodeGenerator(), deleting context from stack: " << toDelete << "\n";
            delete toDelete;
        } else {
            std::cout << "[LOG] CodeGenerator::~CodeGenerator(), context from stack " << toDelete << " is globalContext, not deleting.\n";
        }
    }
    std::cout << "[LOG] CodeGenerator::~CodeGenerator() END\n";
}


void CodeGenerator::initializeModule() {
    // Declare printf function (for internal use, varargs)
    std::vector<llvm::Type*> printfArgs;
    printfArgs.push_back(llvm::Type::getInt8PtrTy(*TheContext));  // char*
    llvm::FunctionType* printfType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*TheContext),  // Return type
        printfArgs,                           // Param types
        true                                  // varargs
    );
    llvm::Function::Create(
        printfType,
        llvm::Function::ExternalLinkage,
        "printf",
        TheModule
    );

    declareRangeFunction();
    implementRangeFunction(); // Implement range function immediately
    declareAndImplementRangeSizeMethod(); // Add size method for range
    // No declaramos funciones fijas de iterador aquí
    // Las funciones de iterador se crearán dinámicamente por cada iterador

    // Declare math functions (using double for standard library compatibility)
    // sqrt - standard library square root function
    llvm::FunctionType* sqrtType = llvm::FunctionType::get(
        llvm::Type::getDoubleTy(*TheContext),   // Return type
        {llvm::Type::getDoubleTy(*TheContext)},  // Param types
        false                                    // not varargs
    );
    llvm::Function::Create(
        sqrtType,
        llvm::Function::ExternalLinkage,
        "sqrt",
        TheModule
    );
    
    // sin - standard library sine function
    llvm::FunctionType* sinType = llvm::FunctionType::get(
        llvm::Type::getDoubleTy(*TheContext),   // Return type
        {llvm::Type::getDoubleTy(*TheContext)},  // Param types
        false                                    // not varargs
    );
    llvm::Function::Create(
        sinType,
        llvm::Function::ExternalLinkage,
        "sin",
        TheModule
    );
    
    // cos - standard library cosine function
    llvm::FunctionType* cosType = llvm::FunctionType::get(
        llvm::Type::getDoubleTy(*TheContext),   // Return type
        {llvm::Type::getDoubleTy(*TheContext)},  // Param types
        false                                    // not varargs
    );
    llvm::Function::Create(
        cosType,
        llvm::Function::ExternalLinkage,
        "cos",
        TheModule
    );
    
    // pow - standard library power function
    llvm::FunctionType* powType = llvm::FunctionType::get(
        llvm::Type::getDoubleTy(*TheContext),   // Return type
        {llvm::Type::getDoubleTy(*TheContext), llvm::Type::getDoubleTy(*TheContext)},  // Param types
        false                                    // not varargs
    );
    llvm::Function::Create(
        powType,
        llvm::Function::ExternalLinkage,
        "pow",
        TheModule
    );
    
    // rand - standard library random number function
    llvm::FunctionType* randType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*TheContext),   // Return type
        {},                                     // No parameters
        false                                   // not varargs
    );
    llvm::Function::Create(
        randType,
        llvm::Function::ExternalLinkage,
        "rand",
        TheModule
    );

    // Declare string concatenation functions
    llvm::Type* i8PtrTy = llvm::Type::getInt8PtrTy(*TheContext);
    
    // string_concat function
    llvm::FunctionType* concatType = llvm::FunctionType::get(
        i8PtrTy,  // Return type: i8*
        {i8PtrTy, i8PtrTy},  // Parameters: i8*, i8*
        false     // Not varargs
    );
    llvm::Function::Create(
        concatType,
        llvm::Function::ExternalLinkage,
        "string_concat",
        TheModule
    );
    
    // string_concat_with_space function
    llvm::Function::Create(
        concatType,
        llvm::Function::ExternalLinkage,
        "string_concat_with_space",
        TheModule
    );
    
    // Declare malloc function
    llvm::FunctionType* mallocType = llvm::FunctionType::get(
        llvm::Type::getInt8PtrTy(*TheContext),
        {llvm::Type::getInt64Ty(*TheContext)},
        false
    );
    llvm::Function::Create(
        mallocType,
        llvm::Function::ExternalLinkage,
        "malloc",
        TheModule
    );
}

void CodeGenerator::initializeUtilityFunctions() {
    // No need for separate print functions as we now use printf directly
}


bool CodeGenerator::generateCode(Node* root) {
    std::cout << "=== Starting code generation ===\n";
    if (!root) {
        std::cout << "Root node is null!\n";
        return false;
    }

    // 1. Crear la función main primero
    llvm::FunctionType* mainType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*TheContext),
        {},
        false
    );
    llvm::Function* mainFunc = llvm::Function::Create(
        mainType,
        llvm::Function::ExternalLinkage,
        "main",
        TheModule
    );

    std::cout << "[LOG] CodeGenerator::generateCode, mainFunc=" << mainFunc << "\n";

    // 2. Paso 1: Generar solo declaraciones (fuera de main)
    std::cout << "[LOG] CodeGenerator::generateCode, calling root->generateDeclarations\n";
    root->generateDeclarations(*this);

    std::cout << "[LOG] CodeGenerator::generateCode, calling root->generateExecutableCode (onlyFunctions=true)\n";
    root->generateExecutableCode(*this, /*onlyFunctions=*/true);

    // 3. Crear bloque de entrada para main
    llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(
        *TheContext,
        "entry",
        mainFunc
    );

    std::cout << "[LOG] CodeGenerator::generateCode, entryBlock=" << entryBlock << "\n";

    // 4. Paso 2: Generar código ejecutable dentro de main
    Builder->SetInsertPoint(entryBlock);

    // Add type registration calls at the beginning of main
    addTypeRegistrationCalls();

    pushScope();

    llvm::Value* retVal = nullptr;
    bool success = true;
    try {
        std::cout << "[LOG] CodeGenerator::generateCode, calling root->generateExecutableCode (onlyFunctions=false)\n";
        retVal = root->generateExecutableCode(*this, /*onlyFunctions=*/false);
        if (!retVal) {
            std::cerr << "Error: se intentó generar código para una expresión inválida" << std::endl;
            // Error path: try to terminate current block with ret 1
            if (Builder->GetInsertBlock() && Builder->GetInsertBlock()->getParent() == mainFunc && !Builder->GetInsertBlock()->getTerminator()) {
                Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1));
            } else if (mainFunc && !mainFunc->empty() && !mainFunc->back().getTerminator()) { // Fallback to last block of main
                Builder->SetInsertPoint(&mainFunc->back());
                Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1));
            } else if (!entryBlock->getTerminator()){ // Ultimate fallback to entry block
                Builder->SetInsertPoint(entryBlock);
                Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1));
            }
            success = false;
        } else {
            std::cout << "Root returned value of type: ";
            retVal->getType()->print(llvm::errs());
            std::cout << "\n";
            // Success path: terminate current block with ret 0
            llvm::BasicBlock* currentBlock = Builder->GetInsertBlock();
            if (currentBlock && currentBlock->getParent() == mainFunc && !currentBlock->getTerminator()) {
                Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 0));
            } else if (mainFunc && !mainFunc->empty() && !mainFunc->back().getTerminator()) {
                // Fallback if currentBlock is somehow not good, try to terminate the last actual block of main.
                Builder->SetInsertPoint(&mainFunc->back());
                if (!mainFunc->back().getTerminator()) { // Check again after setting insert point
                    Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 0));
                }
            } else if (currentBlock && currentBlock->getParent() == mainFunc && currentBlock->getTerminator()) {
                // Current block is already terminated, nothing to do.
            } else if (!entryBlock->getTerminator()) {
                // If entry block itself is not terminated (e.g. completely empty main function)
                Builder->SetInsertPoint(entryBlock);
                Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 0));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception during codegen: " << e.what() << "\n";
        if (Builder->GetInsertBlock() && Builder->GetInsertBlock()->getParent() == mainFunc && !Builder->GetInsertBlock()->getTerminator()) {
            Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1));
        } else if (mainFunc && !mainFunc->empty() && !mainFunc->back().getTerminator()) {
            Builder->SetInsertPoint(&mainFunc->back());
            Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1));
        } else if (!entryBlock->getTerminator()) {
            Builder->SetInsertPoint(entryBlock);
            Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1));
        }
        success = false;
    } catch (...) {
        std::cerr << "Unknown exception during codegen\n";
        if (Builder->GetInsertBlock() && Builder->GetInsertBlock()->getParent() == mainFunc && !Builder->GetInsertBlock()->getTerminator()) {
            Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1));
        } else if (mainFunc && !mainFunc->empty() && !mainFunc->back().getTerminator()) {
            Builder->SetInsertPoint(&mainFunc->back());
            Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1));
        } else if (!entryBlock->getTerminator()) {
            Builder->SetInsertPoint(entryBlock);
            Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1));
        }
        success = false;
    }

    popScope(); // Ensure scope is popped regardless of success/failure path

    std::cout << "\n=== Block Terminator Status ===\n";
    for (auto& func : *TheModule) { // Iterate all functions for comprehensive check
        std::cout << "Function '" << func.getName().str() << "':\n";
        for (auto& block : func) {
            std::cout << "  Block '" << block.getName().str() << "': ";
            if (auto* term = block.getTerminator()) {
                term->print(llvm::outs());
            } else {
                std::cout << "NO TERMINATOR";
            }
            std::cout << "\n";
        }
    }

    if (!success) { // If any error path was taken
        std::cerr << "Code generation failed due to errors or exceptions.\n";
        return false;
    }

    std::cout << "Verifying module...\n";
    std::string errorString;
    llvm::raw_string_ostream errorStream(errorString);
    if (llvm::verifyModule(*TheModule, &errorStream)) {
        std::cerr << "Module verification failed!\n";
        llvm::errs() << "Error in generated module:\n" << errorString;
        std::cerr << "\nGenerated main function:\n";
        mainFunc->print(llvm::errs());
        return false;
    }
    
    std::cout << "=== Code generation completed successfully ===\n";
    return true;
}

// Save module to file
bool CodeGenerator::saveModuleToFile(const std::string& filename) {
    std::error_code EC;
    llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);
    
    if (EC) {
        llvm::errs() << "Could not open file: " << EC.message();
        return false;
    }
    
    TheModule->print(dest, nullptr);
    return true;
}

// Dump LLVM IR to standard output
void CodeGenerator::dumpCode() {
    TheModule->print(llvm::outs(), nullptr);
}

llvm::Function* CodeGenerator::getMainFunction() {
    return TheModule->getFunction("main");
}



llvm::Value* CodeGenerator::generatePrintCall(llvm::Value* value) {
    std::cout << "Generating print call for value of type: ";
    value->getType()->print(llvm::errs());
    std::cout << "\n";

    llvm::IRBuilder<>* builder = getBuilder();
    llvm::Value* strToPrint = nullptr;
    llvm::Value* formatStr = nullptr;
    std::vector<llvm::Value*> printfArgs;

    if (value->getType()->isPointerTy() &&
        value->getType()->getPointerElementType()->isIntegerTy(8)) {
        std::cout << "Printing string value\n";
        


        value->print(llvm::errs());
        std::cout << std::endl;
        
        strToPrint = value;
        formatStr = createGlobalString("%s\n");
    } else if (value->getType()->isIntegerTy(1)) {
        // Handle boolean values (i1) - convert to "true" or "false"
        std::cout << "Printing boolean value\n";
        
        // Create string constants for true and false
        llvm::Value* trueStr = createGlobalString("true");
        llvm::Value* falseStr = createGlobalString("false");
        
        // Create a select instruction to choose between "true" and "false"
        strToPrint = builder->CreateSelect(value, trueStr, falseStr, "bool_str");
        formatStr = createGlobalString("%s\n");
    } else if (value->getType()->isIntegerTy(32)) {
        std::cout << "Printing integer value\n";
        formatStr = createGlobalString("%d\n");
        strToPrint = value;
    } else if (value->getType()->isFloatTy()) {
        std::cout << "Printing float value\n";
        


        value->print(llvm::errs());
        std::cout << std::endl;
        
        // Convert float to double for printf (printf expects double for %f)
        llvm::Value* doubleVal = builder->CreateFPExt(value, llvm::Type::getDoubleTy(*TheContext), "float_to_double");
        formatStr = createGlobalString("%g\n");  // %g removes trailing zeros
        strToPrint = doubleVal;
    } else if (value->getType()->isDoubleTy()) {
        std::cout << "Printing double value\n";
        formatStr = createGlobalString("%g\n");  // %g removes trailing zeros
        strToPrint = value;
    } else {
        std::cout << "Unsupported type for printing\n";
        SEMANTIC_ERROR("No se puede imprimir este tipo aún", SourceLocation());
        return nullptr;
    }

    printfArgs.push_back(formatStr);
    printfArgs.push_back(strToPrint);

    llvm::Function* printfFunc = TheModule->getFunction("printf");
    std::cout << "Calling printf function\n";
    


    for (size_t i = 0; i < printfArgs.size(); ++i) {

        printfArgs[i]->print(llvm::errs());
        std::cout << std::endl;
    }
    
    return builder->CreateCall(printfFunc, printfArgs);
}

// Implementación faltante de declareSprintf
llvm::Function* CodeGenerator::declareSprintf() {
    llvm::Function* func = TheModule->getFunction("sprintf");
    if (!func) {
        std::vector<llvm::Type*> args;
        args.push_back(llvm::Type::getInt8PtrTy(*TheContext)); // char* buffer
        args.push_back(llvm::Type::getInt8PtrTy(*TheContext)); // char* format
        llvm::FunctionType* sprintfType = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(*TheContext), // return int
            args,
            true // varargs
        );
        func = llvm::Function::Create(
            sprintfType,
            llvm::Function::ExternalLinkage,
            "sprintf",
            TheModule
        );
    }
    return func;
}

// Crea una cadena global constante y retorna un i8* a su inicio
llvm::Value* CodeGenerator::createGlobalString(const std::string& str) {
    llvm::IRBuilder<>* builder = getBuilder();

    // Usa el método CreateGlobalStringPtr para evitar duplicados
    return builder->CreateGlobalStringPtr(str, "fmtstr");
}

// Crea un alloca en el entry block del tipo dado y nombre dado
llvm::AllocaInst* CodeGenerator::createEntryBlockAlloca(llvm::Function* function, const std::string& varName, llvm::Type* type) {
    llvm::IRBuilder<> tmpBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
    return tmpBuilder.CreateAlloca(type, nullptr, varName);
}

// Sobrecarga para crear un alloca de i8* (buffer de string)
llvm::AllocaInst* CodeGenerator::createEntryBlockAlloca(llvm::Function* function, llvm::StringRef varName) {
    llvm::IRBuilder<> tmpBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
    return tmpBuilder.CreateAlloca(llvm::Type::getInt8PtrTy(*TheContext), nullptr, varName);
}

// Crea un buffer local de [size x i8] y retorna un i8* al inicio
llvm::Value* CodeGenerator::createStringBuffer(int size) {
    llvm::Function* func = getBuilder()->GetInsertBlock()->getParent();
    llvm::AllocaInst* buffer = createEntryBlockAlloca(
        func,
        "strbuf",
        llvm::ArrayType::get(llvm::Type::getInt8Ty(*TheContext), size)
    );
    // Convierte [size x i8]* a i8*
    llvm::IRBuilder<>* builder = getBuilder();
    llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 0);
    std::vector<llvm::Value*> indices = {zero, zero};
    return builder->CreateInBoundsGEP(
        buffer->getAllocatedType(),
        buffer,
        indices,
        "strbufptr"
    );
}

// Convierte un valor entero (i32) a una cadena (i8*)
llvm::Value* CodeGenerator::intToString(llvm::Value* intVal) {
    // 1. Crear buffer (por ejemplo, 32 bytes)
    int bufferSize = 32;
    llvm::Value* buffer = createStringBuffer(bufferSize); // Debe devolver i8*

    // 2. Obtener sprintf (o snprintf)
    llvm::Function* sprintfFunc = declareSprintf();

    // 3. Crear formato "%d"
    llvm::Value* formatStr = createGlobalString("%d");

    // 4. Llamar a sprintf(buffer, "%d", intVal)
    std::vector<llvm::Value*> args;
    args.push_back(buffer);
    args.push_back(formatStr);
    args.push_back(intVal);

    getBuilder()->CreateCall(sprintfFunc, args, "sprintfcall");

    // 5. Retornar el buffer (i8*)
    return buffer;
}

// Convierte un valor float a una cadena (i8*)
llvm::Value* CodeGenerator::floatToString(llvm::Value* floatVal) {
    // 1. Crear buffer (por ejemplo, 32 bytes)
    int bufferSize = 32;
    llvm::Value* buffer = createStringBuffer(bufferSize); // Debe devolver i8*

    // 2. Obtener sprintf (o snprintf)
    llvm::Function* sprintfFunc = declareSprintf();

    // 3. Crear formato "%g" (removes trailing zeros)
    llvm::Value* formatStr = createGlobalString("%g");

    // 4. Convert float to double for sprintf (sprintf expects double for %g)
    llvm::Value* doubleVal = getBuilder()->CreateFPExt(floatVal, llvm::Type::getDoubleTy(*TheContext), "float_to_double");

    // 5. Llamar a sprintf(buffer, "%g", doubleVal)
    std::vector<llvm::Value*> args;
    args.push_back(buffer);
    args.push_back(formatStr);
    args.push_back(doubleVal);

    getBuilder()->CreateCall(sprintfFunc, args, "sprintfcall");

    // 6. Retornar el buffer (i8*)
    return buffer;
}

// Manejo de scopes
void CodeGenerator::pushScope() {
    std::cout << "[LOG] CodeGenerator::pushScope, contextStack.size()=" << contextStack.size() << "\n";
    IContext* parent = contextStack.empty() ? contextObject : contextStack.back();
    std::cout << "[LOG] CodeGenerator::pushScope, parent=" << parent << "\n";
    // Defensive: parent must be valid and not a dangling pointer!
    if (parent == nullptr) {
        std::cerr << "Error: parent context is null in pushScope" << std::endl;
        // Create a new context if parent is null
        parent = contextObject = new Context(nullptr, TheContext);
    }
    // Defensive: try/catch to detect use-after-free
    IContext* child = nullptr;
    try {
        child = parent->createChildContext();
    } catch (...) {
        std::cerr << "Fatal: parent context is invalid or deleted in pushScope. Aborting.\n";
        std::abort();
    }
    std::cout << "[LOG] CodeGenerator::pushScope, child=" << child << "\n";
    contextStack.push_back(child);
    std::cout << "[LOG] CodeGenerator::pushScope, contextStack.size() now=" << contextStack.size() << "\n";
}

void CodeGenerator::popScope() {
    std::cout << "[LOG] CodeGenerator::popScope, contextStack.size()=" << contextStack.size() << "\n";
    if (!contextStack.empty()) {
        IContext* toDelete = contextStack.back();
        contextStack.pop_back();
        std::cout << "[LOG] CodeGenerator::popScope, contextStack.size() now=" << contextStack.size() << "\n";
        // Only delete if it's not the same as contextObject
        if (toDelete != contextObject) {
            delete toDelete;
        }
    }
    // After popping, ensure contextObject is not dangling
    if (contextStack.empty()) {
    }
}

IContext* CodeGenerator::currentContext() {

    
    // First try the stack
    if (!contextStack.empty()) {
        IContext* stackContext = contextStack.back();

        return stackContext;
    }
    // Then try contextObject
    if (contextObject) {

        return contextObject;
    }
    // Finally, create a new context if needed

    contextObject = new Context(nullptr, TheContext);
    return contextObject;
}

// Implementación de la función range
void CodeGenerator::implementRangeFunction() {
    // Implementación de range(i32, i32) -> %struct.range*
    llvm::Function* rangeFunc = declareRangeFunction();
    
    // Solo implementamos si no tiene cuerpo todavía
    if (rangeFunc->empty()) {
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(*TheContext, "entry", rangeFunc);
        // Usar un builder local para esta función
        llvm::IRBuilder<> rangeBuilder(entry);

        llvm::Value* start = rangeFunc->getArg(0);
        llvm::Value* end = rangeFunc->getArg(1);

        // Declarar malloc si no existe
        llvm::Function* mallocFunc = TheModule->getFunction("malloc");
        if (!mallocFunc) {
            llvm::FunctionType* mallocType = llvm::FunctionType::get(
                llvm::Type::getInt8PtrTy(*TheContext),
                {llvm::Type::getInt64Ty(*TheContext)},
                false
            );
            mallocFunc = llvm::Function::Create(
                mallocType,
                llvm::Function::ExternalLinkage,
                "malloc",
                TheModule
            );
        }

        // Calcular tamaño del struct
        llvm::StructType* rangeType = getRangeStructType(*TheContext);
        llvm::DataLayout dataLayout(TheModule);
        uint64_t structSize = dataLayout.getTypeAllocSize(rangeType);
        llvm::Value* sizeVal = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*TheContext), structSize);
        
        // Llamar a malloc para asignar memoria en el heap
        llvm::Value* mallocResult = rangeBuilder.CreateCall(mallocFunc, {sizeVal}, "malloc.result");
        
        // Convertir el resultado i8* a struct.range*
        llvm::Value* rangePtr = rangeBuilder.CreateBitCast(mallocResult, rangeType->getPointerTo(), "range");

        // Almacenar valores en la estructura
        llvm::Value* currentPtr = rangeBuilder.CreateStructGEP(rangeType, rangePtr, 0, "current.ptr");
        rangeBuilder.CreateStore(start, currentPtr);

        llvm::Value* endPtr = rangeBuilder.CreateStructGEP(rangeType, rangePtr, 1, "end.ptr");
        rangeBuilder.CreateStore(end, endPtr);

        rangeBuilder.CreateRet(rangePtr);
    }
}

// Implementación dinámica de funciones de iterador por nombre
void CodeGenerator::implementIteratorFunctions(const std::string& iterName) {
    // 1) Asegurarnos de usar siempre la misma global:
    llvm::GlobalVariable* rangePtrGlobal = getIteratorGlobalVariable(iterName);

    // 2) Asegurarnos de que exista la función range implementada
    implementRangeFunction();

    // Preparamos tipos y nombres de función
    llvm::StructType* rangeType = getRangeStructType(*TheContext);
    llvm::PointerType* ptrType = rangeType->getPointerTo();
    std::string nextName = getIterNextFunctionName(iterName);
    std::string currName = getIterCurrentFunctionName(iterName);

    // === __iter.next() ===
    llvm::Function* nextFunc = TheModule->getFunction(nextName);
    if (!nextFunc) {
        llvm::FunctionType* fnTy = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(*TheContext), {}, false);
        nextFunc = llvm::Function::Create(
            fnTy, llvm::Function::ExternalLinkage, nextName, TheModule);
    }
    
    // Only implement if the function is empty
    if (nextFunc->empty()) {
        llvm::BasicBlock* bb = llvm::BasicBlock::Create(*TheContext, "entry", nextFunc);
        llvm::IRBuilder<> B(bb);

        // Add null pointer check first
        llvm::Value* loaded = B.CreateLoad(ptrType, rangePtrGlobal, "range.ptr");
        llvm::Value* isNull = B.CreateICmpEQ(loaded, 
            llvm::ConstantPointerNull::get(ptrType), "is.null");
        
        // Create blocks for null check
        llvm::BasicBlock* nullBB = llvm::BasicBlock::Create(*TheContext, "null", nextFunc);
        llvm::BasicBlock* validBB = llvm::BasicBlock::Create(*TheContext, "valid", nextFunc);
        
        B.CreateCondBr(isNull, nullBB, validBB);
        
        // If null, return 0 (no more iterations)
        B.SetInsertPoint(nullBB);
        B.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 0));
        
        // If valid, proceed with normal logic
        B.SetInsertPoint(validBB);
        llvm::Value* curPtr = B.CreateStructGEP(rangeType, loaded, 0, "current.ptr");
        llvm::Value* curVal = B.CreateLoad(llvm::Type::getInt32Ty(*TheContext), curPtr, "current");
        llvm::Value* endPtr = B.CreateStructGEP(rangeType, loaded, 1, "end.ptr");
        llvm::Value* endVal = B.CreateLoad(llvm::Type::getInt32Ty(*TheContext), endPtr, "end");

        // PRIMERO verificamos si current < end (hay más valores)
        llvm::Value* hasMore = B.CreateICmpSLT(curVal, endVal, "has.more");
        
        // Crear bloques para el condicional
        llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(*TheContext, "then", nextFunc);
        llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(*TheContext, "else", nextFunc);
        
        B.CreateCondBr(hasMore, thenBB, elseBB);
        
        // Si hay más valores, incrementamos para la PRÓXIMA iteración
        B.SetInsertPoint(thenBB);
        llvm::Value* nextVal = B.CreateAdd(curVal,
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1),
            "next.val");
        B.CreateStore(nextVal, curPtr);
        B.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1));
        
        // Si no hay más valores, retornamos 0
        B.SetInsertPoint(elseBB);
        B.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 0));
    }

    // === __iter.current() ===
    llvm::Function* currentFunc = TheModule->getFunction(currName);
    if (!currentFunc) {
        llvm::FunctionType* fnTy = llvm::FunctionType::get(
            llvm::Type::getFloatTy(*TheContext), {}, false);
        currentFunc = llvm::Function::Create(
            fnTy, llvm::Function::ExternalLinkage, currName, TheModule);
    }
    
    // Only implement if the function is empty
    if (currentFunc->empty()) {
        llvm::BasicBlock* bb = llvm::BasicBlock::Create(*TheContext, "entry", currentFunc);
        llvm::IRBuilder<> B(bb);

        // Add null pointer check first
        llvm::Value* loaded = B.CreateLoad(ptrType, rangePtrGlobal, "range.ptr");
        llvm::Value* isNull = B.CreateICmpEQ(loaded, 
            llvm::ConstantPointerNull::get(ptrType), "is.null");
        
        // Create blocks for null check
        llvm::BasicBlock* nullBB = llvm::BasicBlock::Create(*TheContext, "null", currentFunc);
        llvm::BasicBlock* validBB = llvm::BasicBlock::Create(*TheContext, "valid", currentFunc);
        
        B.CreateCondBr(isNull, nullBB, validBB);
        
        // If null, return 0.0
        B.SetInsertPoint(nullBB);
        B.CreateRet(llvm::ConstantFP::get(llvm::Type::getFloatTy(*TheContext), 0.0));
        
        // If valid, proceed with normal logic
        B.SetInsertPoint(validBB);

        // Buscar el array global con los valores del vector
        std::string arrayName = "__vector_data_" + iterName;
        llvm::GlobalVariable* vectorArray = TheModule->getGlobalVariable(arrayName);
        
        // If not found with iterator name, search through all existing vector data arrays
        if (!vectorArray) {


            for (auto &G : TheModule->globals()) {
                std::string globalName = G.getName().str();

                if (globalName.find("__vector_data_") == 0) {

                    // Use the first vector data array we find
                    vectorArray = &G;
                    arrayName = globalName;
                    break;
                }
            }
        }
        
        if (vectorArray) {

            
            // Load the current index from the range struct
            llvm::Value* curPtr = B.CreateStructGEP(rangeType, loaded, 0, "current.ptr");
            llvm::Value* valFromStruct = B.CreateLoad(llvm::Type::getInt32Ty(*TheContext), curPtr, "val.from.struct");
            
            // Subtract 1 to get the correct index (since next() increments first)
            llvm::Value* actualCurrentVal = B.CreateSub(valFromStruct, 
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1), 
                "actual.current");
            
            // Add bounds check to ensure we don't access out of bounds
            llvm::Value* isNegative = B.CreateICmpSLT(actualCurrentVal, 
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 0),
                "is_negative");
            
            // Get the actual array size from the global array type
            llvm::Type* arrayType = vectorArray->getValueType();
            int arraySize = 0;
            if (auto* arrType = llvm::dyn_cast<llvm::ArrayType>(arrayType)) {
                arraySize = arrType->getNumElements();

            } else {
                // Fallback: try to get size from stored vector data
                std::vector<double> vectorValues;
                if (hasVectorDataForIterator(iterName)) {
                    vectorValues = getVectorDataForIterator(iterName);
                    arraySize = vectorValues.size();

                } else {
                    // Last resort: search through all vector data
                    for (const auto& pair : getAllVectorData()) {
                        vectorValues = pair.second;
                        arraySize = vectorValues.size();

                        break;
                    }
                }
            }
            
            llvm::Value* isTooBig = B.CreateICmpSGE(actualCurrentVal,
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), arraySize),
                "is_too_big");
                
            llvm::Value* isOutOfBounds = B.CreateOr(isNegative, isTooBig, "is_out_of_bounds");
            
            // Create blocks for the bounds check
            llvm::BasicBlock* inBoundsBB = llvm::BasicBlock::Create(*TheContext, "in_bounds", currentFunc);
            llvm::BasicBlock* outOfBoundsBB = llvm::BasicBlock::Create(*TheContext, "out_of_bounds", currentFunc);
            
            B.CreateCondBr(isOutOfBounds, outOfBoundsBB, inBoundsBB);
            
            // Out of bounds case - return 0.0
            B.SetInsertPoint(outOfBoundsBB);
            B.CreateRet(llvm::ConstantFP::get(llvm::Type::getFloatTy(*TheContext), 0.0));
            
            // In bounds case - return the vector element
            B.SetInsertPoint(inBoundsBB);
            
            // Get the value at the current index
            llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 0);
            std::vector<llvm::Value*> indices = {zero, actualCurrentVal};
            llvm::Value* elementPtr = B.CreateInBoundsGEP(
                vectorArray->getValueType(),
                vectorArray,
                indices,
                "element_ptr"
            );
            llvm::Value* elementValue = B.CreateLoad(llvm::Type::getDoubleTy(*TheContext), elementPtr, "element_value");
            
            // Convert double to float for return type consistency
            llvm::Value* floatValue = B.CreateFPTrunc(elementValue, llvm::Type::getFloatTy(*TheContext), "double_to_float");
            

            actualCurrentVal->print(llvm::errs());
            std::cout << std::endl;
            
            B.CreateRet(floatValue);
        } else {
            // No vector data array found, fall back to range-based iteration
            // Load the current index from the range struct
            llvm::Value* curPtr = B.CreateStructGEP(rangeType, loaded, 0, "current.ptr");
            llvm::Value* valFromStruct = B.CreateLoad(llvm::Type::getInt32Ty(*TheContext), curPtr, "val.from.struct");
            
            // Subtract 1 to get the correct index (since next() increments first)
            llvm::Value* actualCurrentVal = B.CreateSub(valFromStruct, 
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(*TheContext), 1), 
                "actual.current");
                
            // Convert integer to float for return type consistency
            llvm::Value* floatVal = B.CreateSIToFP(actualCurrentVal, llvm::Type::getFloatTy(*TheContext), "int_to_float");
                

            B.CreateRet(floatVal);
        }
    }
}

// Method to store vector data for iteration
void CodeGenerator::storeVectorDataForIterator(const std::string& iterName, const std::vector<double>& values) {

    for (double val : values) {
        std::cout << val << " ";
    }
    std::cout << std::endl;
    
    // Store the vector data in our map
    vectorData[iterName] = values;
    
    // Also create a global array with the vector values for direct access
    std::string arrayName = "__vector_data_" + iterName;
    llvm::GlobalVariable* existingArray = TheModule->getGlobalVariable(arrayName);
    
    // Only create if it doesn't exist yet
    if (!existingArray && !values.empty()) {
        std::vector<llvm::Constant*> constants;
        for (double val : values) {
            constants.push_back(llvm::ConstantFP::get(llvm::Type::getDoubleTy(*TheContext), val));
        }
        
        llvm::ArrayType* arrayType = llvm::ArrayType::get(
            llvm::Type::getDoubleTy(*TheContext), 
            values.size()
        );
        
        llvm::Constant* arrayInit = llvm::ConstantArray::get(arrayType, constants);
        
        new llvm::GlobalVariable(
            *TheModule,
            arrayType,
            true, // constant
            llvm::GlobalValue::InternalLinkage,
            arrayInit,
            arrayName
        );
        

    }
}

// Check if vector data exists for an iterator
bool CodeGenerator::hasVectorDataForIterator(const std::string& iterName) const {
    return vectorData.find(iterName) != vectorData.end();
}

// Get vector data for an iterator
std::vector<double> CodeGenerator::getVectorDataForIterator(const std::string& iterName) const {
    auto it = vectorData.find(iterName);
    if (it != vectorData.end()) {
        return it->second;
    }
    return std::vector<double>(); // Return empty vector if not found
}

// Get all vector data
const std::map<std::string, std::vector<double>>& CodeGenerator::getAllVectorData() const {
    return vectorData;
}

// Declaración de la función range
llvm::Function* CodeGenerator::declareRangeFunction() {
    llvm::Function* func = TheModule->getFunction("range");
    if (!func) {
        llvm::StructType* rangeType = getRangeStructType(*TheContext);
        std::vector<llvm::Type*> args = {
            llvm::Type::getInt32Ty(*TheContext), // start
            llvm::Type::getInt32Ty(*TheContext)  // end
        };
        llvm::FunctionType* funcType = llvm::FunctionType::get(
            rangeType->getPointerTo(), // return type: %struct.range*
            args,
            false
        );
        func = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            "range",
            TheModule
        );
    }
    return func;
}

llvm::GlobalVariable* CodeGenerator::getIteratorGlobalVariable(const std::string& iterName) {
    std::string globalName = getIteratorGlobalName(iterName);

    static std::map<std::string, llvm::GlobalVariable*> iteratorGlobals;
    
    // First check if it's in our map
    auto it = iteratorGlobals.find(globalName);
    if (it != iteratorGlobals.end()) {
        return it->second;
    }
    
    // Then check if it already exists in the module
    llvm::GlobalVariable* existingGlobal = TheModule->getGlobalVariable(globalName);
    if (existingGlobal) {
        // Store it in our map for future lookups
        iteratorGlobals[globalName] = existingGlobal;
        return existingGlobal;
    }
    


    for (auto &G : TheModule->globals()) {
        std::cout << "  - " << G.getName().str() << " at " << &G << std::endl;
    }
    
    // If not, create it ONCE
    llvm::StructType* rangeType = getRangeStructType(*TheContext);
    llvm::PointerType* rangePtrType = rangeType->getPointerTo();
    
    llvm::GlobalVariable* newGlobal = new llvm::GlobalVariable(
        *TheModule,
        rangePtrType,
        false, // not constant
        llvm::GlobalValue::InternalLinkage,
        llvm::ConstantPointerNull::get(rangePtrType),
        globalName
    );
    
    // Store the new global in our map
    iteratorGlobals[globalName] = newGlobal;
    

    std::string nextName = getIterNextFunctionName(iterName);
    std::string currName = getIterCurrentFunctionName(iterName);
    
    // Create function declarations only - implementation will happen in implementIteratorFunctions
    if (!TheModule->getFunction(nextName)) {
        llvm::FunctionType* fnTy = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(*TheContext), {}, false);
        llvm::Function::Create(
            fnTy, llvm::Function::ExternalLinkage, nextName, TheModule);
    }
    
    if (!TheModule->getFunction(currName)) {
        llvm::FunctionType* fnTy = llvm::FunctionType::get(
            llvm::Type::getFloatTy(*TheContext), {}, false);
        llvm::Function::Create(
            fnTy, llvm::Function::ExternalLinkage, currName, TheModule);
    }
    
    return newGlobal;
}

// Declare and implement the size method for range type
void CodeGenerator::declareAndImplementRangeSizeMethod() {
    // Create the size method for range: range.size() -> i32
    llvm::StructType* rangeType = getRangeStructType(*TheContext);
    llvm::PointerType* rangePtrType = rangeType->getPointerTo();
    
    // Function signature: i32 range.size(%struct.range* %self)
    llvm::FunctionType* sizeMethodType = llvm::FunctionType::get(
        llvm::Type::getInt32Ty(*TheContext),  // Return type: i32
        {rangePtrType},                       // Parameters: %struct.range* self
        false                                 // Not varargs
    );
    
    llvm::Function* sizeMethod = llvm::Function::Create(
        sizeMethodType,
        llvm::Function::ExternalLinkage,
        "range.size",
        TheModule
    );
    
    // Implement the size method
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*TheContext, "entry", sizeMethod);
    llvm::IRBuilder<> sizeBuilder(entry);
    
    // Get the range parameter
    llvm::Value* rangePtr = sizeMethod->getArg(0);
    
    // Get the current and end values from the range struct
    llvm::Value* currentPtr = sizeBuilder.CreateStructGEP(rangeType, rangePtr, 0, "current.ptr");
    
    llvm::Value* endPtr = sizeBuilder.CreateStructGEP(rangeType, rangePtr, 1, "end.ptr");
    llvm::Value* endVal = sizeBuilder.CreateLoad(llvm::Type::getInt32Ty(*TheContext), endPtr, "end");
    
    llvm::Value* size = endVal;
    


    
    sizeBuilder.CreateRet(size);
    
    // Register the method in the context
    if (contextObject) {
        contextObject->addMethod("range", "size", sizeMethod);
    }
}

void CodeGenerator::addTypeRegistrationCalls() {
    // Get or create the __hulk_register_type function
    llvm::Function* registerTypeFunc = TheModule->getFunction("__hulk_register_type");
    if (!registerTypeFunc) {
        // Create the function declaration
        llvm::FunctionType* funcType = llvm::FunctionType::get(
            llvm::Type::getVoidTy(*TheContext),
            {llvm::Type::getInt8PtrTy(*TheContext), llvm::Type::getInt8PtrTy(*TheContext)},
            false
        );
        registerTypeFunc = llvm::Function::Create(
            funcType,
            llvm::Function::ExternalLinkage,
            "__hulk_register_type",
            TheModule
        );
    }

    // Register built-in types first
    llvm::Value* objectStr = Builder->CreateGlobalStringPtr("Object");
    llvm::Value* nullStr = Builder->CreateGlobalStringPtr("");
    Builder->CreateCall(registerTypeFunc, {objectStr, nullStr});
    
    llvm::Value* numberStr = Builder->CreateGlobalStringPtr("Number");
    Builder->CreateCall(registerTypeFunc, {numberStr, objectStr});
    
    llvm::Value* stringStr = Builder->CreateGlobalStringPtr("String");
    Builder->CreateCall(registerTypeFunc, {stringStr, objectStr});
    
    llvm::Value* boolStr = Builder->CreateGlobalStringPtr("Boolean");
    Builder->CreateCall(registerTypeFunc, {boolStr, objectStr});

    // Register user-defined types by examining the context
    if (contextObject) {
        // Get all registered types from the context
        auto* context = dynamic_cast<Context*>(contextObject);
        if (context) {
            // Get all type definitions
            const auto& typeDefinitions = context->getTypeDefinitions();
            
            // Register each user-defined type
            for (const auto& pair : typeDefinitions) {
                const std::string& typeName = pair.first;
                TypeDefinition* typeDef = pair.second;
                
                if (typeDef) {
                    std::string parentType = typeDef->getParentType();
                    
                    // Create string constants for type name and parent type
                    llvm::Value* typeNameStr = Builder->CreateGlobalStringPtr(typeName);
                    llvm::Value* parentTypeStr = Builder->CreateGlobalStringPtr(parentType);
                    
                    // Call __hulk_register_type(typeName, parentType)
                    Builder->CreateCall(registerTypeFunc, {typeNameStr, parentTypeStr});
                    
                    std::cout << "Registered type: " << typeName << " with parent: " << parentType << std::endl;
                }
            }
        }
    }
}

