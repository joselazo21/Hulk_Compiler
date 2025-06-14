cmake_minimum_required(VERSION 3.10)
project(Compiler LANGUAGES C CXX)

# Configuración básica
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIE")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fPIE")

# Forzar tipo de build a Debug
set(CMAKE_BUILD_TYPE Debug)

# Find required packages
find_package(BISON REQUIRED)
# Note: We no longer use FLEX, we use our custom lexer generator

# Configuración de LLVM
find_package(LLVM 14.0 REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

# Build the lexer generator first
add_executable(lexer_generator src/lexer_generator.cpp)
target_compile_features(lexer_generator PRIVATE cxx_std_11)

# Generate Bison files
BISON_TARGET(SintacticoBison src/SintacticoBison.ypp ${CMAKE_BINARY_DIR}/SintacticoBison.tab.cpp
    DEFINES_FILE ${CMAKE_BINARY_DIR}/SintacticoBison.tab.hpp)

# Generate custom lexer using our lexer generator
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/custom_lexer.cpp
    COMMAND lexer_generator ${CMAKE_SOURCE_DIR}/hulk_lexer_def.txt ${CMAKE_BINARY_DIR}/custom_lexer.cpp
    DEPENDS lexer_generator ${CMAKE_SOURCE_DIR}/hulk_lexer_def.txt
    COMMENT "Generating custom lexer"
)

# Create a custom target for the lexer generation
add_custom_target(GenerateLexer DEPENDS ${CMAKE_BINARY_DIR}/custom_lexer.cpp)

# Make sure the lexer is generated before building the main executable
add_dependencies(GenerateLexer lexer_generator)

# Incluye directorios como SYSTEM para suprimir warnings
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}
)

# Custom Lexer/Bison sources
set(CUSTOM_LEXER_BISON_SOURCES
    src/main_flex_bison.cpp
    ${BISON_SintacticoBison_OUTPUTS}
    ${CMAKE_BINARY_DIR}/custom_lexer.cpp
    src/code_generator.cpp
    src/print.c
    src/runtime_iter.c
    src/assignment.cpp
    src/ast_utils.cpp
    src/block_expression.cpp
    src/block_statement.cpp
    src/binary_operation.cpp
    src/context.cpp
    src/expression.cpp
    src/if_expression.cpp
    src/let_in.cpp
    src/node.cpp
    src/number.cpp
    src/program.cpp
    src/statement.cpp
    src/string_literal.cpp
    src/variable.cpp
    src/while_expression.cpp
    src/assignment_expression.cpp
    src/type_definition.cpp
    src/new_expression.cpp
    src/member_access.cpp
    src/self_expression.cpp
    src/method_call.cpp
    src/base_expression.cpp
    src/vector_expression.cpp
    src/type_system.cpp
    src/function_declaration.cpp
    src/return_statement.cpp
    src/expression_statement.cpp
    src/function_call.cpp
    src/while_statement.cpp
    src/print.cpp
    src/for_statement.cpp
    src/if_statement.cpp
    src/is_expression.cpp
    src/as_expression.cpp
    src/unary_operation.cpp
)

# Custom Lexer/Bison executable (MAIN)
add_executable(main_flex_bison ${CUSTOM_LEXER_BISON_SOURCES})

# Make sure the custom lexer is generated before building the main executable
add_dependencies(main_flex_bison GenerateLexer)

# Configuración de warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(main_flex_bison PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-ignored-qualifiers
        -Wno-deprecated-declarations
        -Wno-unused-function
        -Wno-sign-compare
    )
    
    if(LLVM_PACKAGE_VERSION VERSION_GREATER_EQUAL "14.0")
        target_compile_options(main_flex_bison PRIVATE
            -Wno-unused-but-set-variable
            -Wno-maybe-uninitialized
        )
    endif()
elseif(MSVC)
    target_compile_options(main_flex_bison PRIVATE
        /W4
        /wd4068
        /wd4146
        /wd4244
        /wd4267
    )
endif()

# Enlazado con bibliotecas
llvm_map_components_to_libnames(llvm_libs
    core
    support
    irreader
    mcjit
    native
    x86codegen
)

# Añade runtime_iter.c como biblioteca estática
add_library(runtime_iter STATIC src/runtime_iter.c)

# Añade print.c como biblioteca estática para las funciones de runtime
add_library(runtime_print STATIC src/print.c)

target_link_libraries(main_flex_bison PRIVATE
    ${llvm_libs}
    ${LLVM_LIBRARIES}
    runtime_iter
)

# Configuraciones adicionales
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(main_flex_bison PRIVATE -g3 -O0)
else()
    target_compile_options(main_flex_bison PRIVATE -O3)
endif()

# Hacer que 'make' por defecto construya y ejecute todo
add_custom_target(build-and-run-custom ALL DEPENDS run-llvm-custom)