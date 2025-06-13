#include "tree.hpp"
#include "error_handler.hpp"
#include <iostream>

// VectorExpression implementation

// Constructor for explicit vectors [1,2,3]
VectorExpression::VectorExpression(const SourceLocation& loc, const std::vector<Expression*>& elements)
    : Expression(loc), elements(elements), generator(nullptr), variable(""), iterable(nullptr), isGenerator(false) {
}

// Constructor for generator vectors [x^2 | x in range(1,10)]
VectorExpression::VectorExpression(const SourceLocation& loc, Expression* generator, const std::string& variable, Expression* iterable)
    : Expression(loc), generator(generator), variable(variable), iterable(iterable), isGenerator(true) {
}

VectorExpression::~VectorExpression() {
    if (isGenerator) {
        delete generator;
        delete iterable;
    } else {
        for (auto* elem : elements) {
            delete elem;
        }
    }
}

std::string VectorExpression::toString() {
    if (isGenerator) {
        return "[" + generator->toString() + " | " + variable + " in " + iterable->toString() + "]";
    } else {
        std::string result = "[";
        for (size_t i = 0; i < elements.size(); ++i) {
            if (i > 0) result += ", ";
            result += elements[i]->toString();
        }
        result += "]";
        return result;
    }
}

void VectorExpression::printNode(int depth) {
    printIndent(depth);
    if (isGenerator) {
        std::cout << "VectorExpression (generator): [" << variable << " in ";
        iterable->printNode(0);
        std::cout << " => ";
        generator->printNode(0);
        std::cout << "]" << std::endl;
    } else {
        std::cout << "VectorExpression (explicit): [" << std::endl;
        for (auto* elem : elements) {
            elem->printNode(depth + 1);
        }
        printIndent(depth);
        std::cout << "]" << std::endl;
    }
}

bool VectorExpression::Validate(IContext* context) {
    if (isGenerator) {
        // Validate the iterable expression
        if (!iterable->Validate(context)) {
            return false;
        }
        
        // Create a new scope for the generator variable
        IContext* childContext = context->createChildContext();
        
        // Add the generator variable to the scope with proper type
        llvm::LLVMContext* llvmCtx = context->getLLVMContext();
        if (!childContext->addVariable(variable, llvm::Type::getInt32Ty(*llvmCtx))) {
            SEMANTIC_ERROR("Failed to add generator variable '" + variable + "' to scope", location);
            delete childContext;
            return false;
        }
        
        // Validate the generator expression in the new scope
        bool result = generator->Validate(childContext);
        
        delete childContext;
        return result;
    } else {
        // Validate all elements
        for (auto* elem : elements) {
            if (!elem->Validate(context)) {
                return false;
            }
        }
        return true;
    }
}

llvm::Value* VectorExpression::codegen(CodeGenerator& generator) {
    if (isGenerator) {
        // Generator vectors are transformed into LetIn expressions by the parser
        // This should not be called directly for generator vectors
        // Instead, the parser transforms [expr | var in iterable] into:
        // let __iter_name = iterable, __result_name = [] in 
        //   while __iter_name.next() do
        //     let var = __iter_name.current() in
        //       __result_name.append(expr)
        
        // For now, we'll generate a simple range-based iteration
        // This is a fallback implementation that shouldn't normally be reached
        
        // Generate the iterable (should be a range or similar)
        llvm::Value* iterableVal = iterable->codegen(generator);
        if (!iterableVal) {
            SEMANTIC_ERROR("Failed to generate code for iterable in generator vector", location);
            return nullptr;
        }
        
        // For simplicity, assume the iterable is a range and return it
        // The actual iteration logic should be handled by the transformed LetIn structure
        return iterableVal;
    } else {
        // Para vectores explícitos, evaluamos los elementos y almacenamos los valores
        std::vector<int> values;
        for (auto* elem : elements) {
            llvm::Value* val = elem->codegen(generator);
            if (!val) {
                SEMANTIC_ERROR("Error generating code for vector element", location);
                return nullptr;
            }
            // Solo soportamos enteros por ahora
            if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(val)) {
                values.push_back(constInt->getSExtValue());
            } else {
                SEMANTIC_ERROR("Only integer literals are supported in explicit vectors for now", location);
                return nullptr;
            }
        }
        // Get the destination variable name from context
        std::string iterName = generator.getCurrentLetInVariableName();
        std::cout << "[DEBUG] VectorExpression::codegen - getCurrentLetInVariableName() returned: '" << iterName << "'" << std::endl;
        if (iterName.empty()) {
            // Fallback for when not in let-in context
            static int anonVecCounter = 0;
            iterName = "anon_vec_" + std::to_string(anonVecCounter++);
            std::cout << "[DEBUG] VectorExpression::codegen - using fallback name: '" << iterName << "'" << std::endl;
        }

        // Store vector data using the determined name
        std::cout << "[DEBUG] VectorExpression::codegen - storing vector data for: '" << iterName << "'" << std::endl;
        std::cout << "[DEBUG] Vector values: ";
        for (int val : values) {
            std::cout << val << " ";
        }
        std::cout << std::endl;
        generator.storeVectorDataForIterator(iterName, values);
        // Creamos un rango para iterar (de 0 a N)
        llvm::Function* rangeFunc = generator.getModule()->getFunction("range");
        if (!rangeFunc) {
            rangeFunc = generator.declareRangeFunction();
            generator.implementRangeFunction();
        }
        llvm::Value* start = llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0);
        llvm::Value* end = llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), values.size());
        std::vector<llvm::Value*> args = {start, end};
        llvm::Value* rangeResult = generator.getBuilder()->CreateCall(rangeFunc, args, "vector_range");
        
        // Asegurarnos de que el rango se asocie con los valores del vector
        std::cout << "[DEBUG] Created range for vector with size: " << values.size() << std::endl;
        
        return rangeResult;
    }
}

// VectorIndexExpression implementation

VectorIndexExpression::VectorIndexExpression(const SourceLocation& loc, Expression* vector, Expression* index)
    : Expression(loc), vector(vector), index(index) {
}

VectorIndexExpression::~VectorIndexExpression() {
    delete vector;
    delete index;
}

std::string VectorIndexExpression::toString() {
    return vector->toString() + "[" + index->toString() + "]";
}

void VectorIndexExpression::printNode(int depth) {
    printIndent(depth);
    std::cout << "VectorIndexExpression:" << std::endl;
    printIndent(depth + 1);
    std::cout << "Vector:" << std::endl;
    vector->printNode(depth + 2);
    printIndent(depth + 1);
    std::cout << "Index:" << std::endl;
    index->printNode(depth + 2);
}

bool VectorIndexExpression::Validate(IContext* context) {
    // Validate both the vector and index expressions
    if (!vector->Validate(context)) {
        return false;
    }
    
    if (!index->Validate(context)) {
        return false;
    }
    
    // TODO: Add type checking to ensure vector is actually a vector type
    // and index is a number
    
    return true;
}

llvm::Value* VectorIndexExpression::codegen(CodeGenerator& generator) {
    // Generate code for vector indexing
    // This is a simplified implementation
    
    llvm::Value* vectorVal = vector->codegen(generator);
    llvm::Value* indexVal = index->codegen(generator);
    
    if (!vectorVal || !indexVal) {
        return nullptr;
    }
    
    // For now, just return the vector value as a placeholder
    // A full implementation would need proper vector indexing
    SEMANTIC_ERROR("Vector indexing not yet fully implemented in code generation", location);
    return vectorVal;
}