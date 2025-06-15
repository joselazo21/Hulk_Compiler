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
        std::vector<double> values;
        for (auto* elem : elements) {
            // Check if the element is a Number node directly
            if (auto* numberNode = dynamic_cast<Number*>(elem)) {
                // Get the value directly from the Number node (keep as double)
                values.push_back(numberNode->getValue());
                std::cout << "[DEBUG] Added number value: " << numberNode->getValue() << std::endl;
            } else {
                // Try to generate code and extract constant
                llvm::Value* val = elem->codegen(generator);
                if (!val) {
                    SEMANTIC_ERROR("Error generating code for vector element", location);
                    return nullptr;
                }
                // Support both integers and floats
                if (auto* constInt = llvm::dyn_cast<llvm::ConstantInt>(val)) {
                    values.push_back(static_cast<double>(constInt->getSExtValue()));
                    std::cout << "[DEBUG] Added constant int value: " << constInt->getSExtValue() << std::endl;
                } else if (auto* constFloat = llvm::dyn_cast<llvm::ConstantFP>(val)) {
                    values.push_back(constFloat->getValueAPF().convertToDouble());
                    std::cout << "[DEBUG] Added constant float value: " << constFloat->getValueAPF().convertToDouble() << std::endl;
                } else {
                    SEMANTIC_ERROR("Only numeric literals are supported in explicit vectors for now", location);
                    return nullptr;
                }
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
        for (double val : values) {
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
    std::cout << "[DEBUG] VectorIndexExpression::codegen - Starting vector indexing" << std::endl;
    
    // First, check if the vector is a variable that has associated vector data
    if (auto* varNode = dynamic_cast<Variable*>(vector)) {
        std::string varName = varNode->getName();
        std::cout << "[DEBUG] VectorIndexExpression::codegen - Vector is variable: " << varName << std::endl;
        
        // Check if this variable has vector data stored
        if (generator.hasVectorDataForIterator(varName)) {
            std::cout << "[DEBUG] VectorIndexExpression::codegen - Found vector data for variable: " << varName << std::endl;
            
            // Generate the index value
            llvm::Value* indexVal = index->codegen(generator);
            if (!indexVal) {
                SEMANTIC_ERROR("Failed to generate index value for vector indexing", location);
                return nullptr;
            }
            
            // Handle both integer and float indices
            llvm::Value* intIndexVal = nullptr;
            if (indexVal->getType()->isIntegerTy(32)) {
                intIndexVal = indexVal;
            } else if (indexVal->getType()->isFloatTy()) {
                // Convert float to int32 for indexing
                intIndexVal = generator.getBuilder()->CreateFPToSI(
                    indexVal, 
                    llvm::Type::getInt32Ty(generator.getContext()),
                    "float_to_int_index"
                );
                std::cout << "[DEBUG] VectorIndexExpression::codegen - Converted float index to integer" << std::endl;
            } else {
                SEMANTIC_ERROR("Vector index must be a number (integer or float)", location);
                return nullptr;
            }
            
            // Get the global array for this vector
            std::string arrayName = "__vector_data_" + varName;
            llvm::GlobalVariable* vectorArray = generator.getModule()->getGlobalVariable(arrayName);
            
            if (!vectorArray) {
                std::cout << "[DEBUG] VectorIndexExpression::codegen - No global array found, searching all globals..." << std::endl;
                // Search through all globals to find a vector data array
                for (auto &G : generator.getModule()->globals()) {
                    std::string globalName = G.getName().str();
                    if (globalName.find("__vector_data_") == 0) {
                        std::cout << "[DEBUG] VectorIndexExpression::codegen - Using vector data global: " << globalName << std::endl;
                        vectorArray = &G;
                        break;
                    }
                }
            }
            
            if (vectorArray) {
                std::cout << "[DEBUG] VectorIndexExpression::codegen - Using global array: " << vectorArray->getName().str() << std::endl;
                
                // Get the vector data to determine bounds
                std::vector<double> vectorData = generator.getVectorDataForIterator(varName);
                if (vectorData.empty()) {
                    // Try to get from any available vector data
                    const auto& allVectorData = generator.getAllVectorData();
                    if (!allVectorData.empty()) {
                        vectorData = allVectorData.begin()->second;
                    }
                }
                
                if (!vectorData.empty()) {
                    // Add bounds checking
                    llvm::Value* arraySize = llvm::ConstantInt::get(
                        llvm::Type::getInt32Ty(generator.getContext()), 
                        vectorData.size()
                    );
                    
                    // Check if index is negative
                    llvm::Value* isNegative = generator.getBuilder()->CreateICmpSLT(
                        intIndexVal, 
                        llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0),
                        "is_negative"
                    );
                    
                    // Check if index is >= array size
                    llvm::Value* isTooBig = generator.getBuilder()->CreateICmpSGE(
                        intIndexVal, arraySize, "is_too_big"
                    );
                    
                    llvm::Value* isOutOfBounds = generator.getBuilder()->CreateOr(
                        isNegative, isTooBig, "is_out_of_bounds"
                    );
                    
                    // Create blocks for bounds checking
                    llvm::Function* currentFunc = generator.getBuilder()->GetInsertBlock()->getParent();
                    llvm::BasicBlock* inBoundsBB = llvm::BasicBlock::Create(
                        generator.getContext(), "vector_in_bounds", currentFunc
                    );
                    llvm::BasicBlock* outOfBoundsBB = llvm::BasicBlock::Create(
                        generator.getContext(), "vector_out_of_bounds", currentFunc
                    );
                    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(
                        generator.getContext(), "vector_continue", currentFunc
                    );
                    
                    generator.getBuilder()->CreateCondBr(isOutOfBounds, outOfBoundsBB, inBoundsBB);
                    
                    // Out of bounds block - return 0.0 (or could throw error)
                    generator.getBuilder()->SetInsertPoint(outOfBoundsBB);
                    llvm::Value* outOfBoundsResult = llvm::ConstantFP::get(
                        llvm::Type::getDoubleTy(generator.getContext()), 0.0
                    );
                    generator.getBuilder()->CreateBr(continueBB);
                    
                    // In bounds block - access the array element
                    generator.getBuilder()->SetInsertPoint(inBoundsBB);
                    
                    // Create GEP to access array element
                    llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(generator.getContext()), 0);
                    std::vector<llvm::Value*> indices = {zero, intIndexVal};
                    
                    llvm::Value* elementPtr = generator.getBuilder()->CreateInBoundsGEP(
                        vectorArray->getValueType(),
                        vectorArray,
                        indices,
                        "element_ptr"
                    );
                    
                    llvm::Value* elementValue = generator.getBuilder()->CreateLoad(
                        llvm::Type::getDoubleTy(generator.getContext()),
                        elementPtr,
                        "element_value"
                    );
                    
                    generator.getBuilder()->CreateBr(continueBB);
                    
                    // Continue block - use PHI to select the result
                    generator.getBuilder()->SetInsertPoint(continueBB);
                    llvm::PHINode* result = generator.getBuilder()->CreatePHI(
                        llvm::Type::getDoubleTy(generator.getContext()), 2, "vector_index_result"
                    );
                    result->addIncoming(outOfBoundsResult, outOfBoundsBB);
                    result->addIncoming(elementValue, inBoundsBB);
                    
                    std::cout << "[DEBUG] VectorIndexExpression::codegen - Successfully generated vector indexing code" << std::endl;
                    return result;
                } else {
                    SEMANTIC_ERROR("No vector data found for variable: " + varName, location);
                    return nullptr;
                }
            } else {
                SEMANTIC_ERROR("No global array found for vector variable: " + varName, location);
                return nullptr;
            }
        } else {
            std::cout << "[DEBUG] VectorIndexExpression::codegen - No vector data for variable: " << varName << std::endl;
        }
    }
    
    // Fallback: try the original approach
    llvm::Value* vectorVal = vector->codegen(generator);
    llvm::Value* indexVal = index->codegen(generator);
    
    if (!vectorVal || !indexVal) {
        SEMANTIC_ERROR("Failed to generate vector or index value", location);
        return nullptr;
    }
    
    // If we reach here, vector indexing is not supported for this case
    SEMANTIC_ERROR("Vector indexing not supported for this vector type", location);
    return nullptr;
}