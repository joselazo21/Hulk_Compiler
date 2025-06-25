#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Enhanced TypeInfo structure matching LLVM struct
typedef struct TypeInfo {
    char* type_name;
    char* parent_type_name;  // Simplified - stores parent type name as string
} TypeInfo;

// Enhanced runtime type checking with inheritance support
int __hulk_runtime_type_check_enhanced(void* object, const char* target_type_name) { 
    if (!object || !target_type_name) {

        return 0;
    }
    
    // The first field of every object is a pointer to TypeInfo
    TypeInfo** type_info_ptr = (TypeInfo**)object;
    TypeInfo* type_info = *type_info_ptr;
    
    if (!type_info || !type_info->type_name) {

        return 0;
    }
    
    // Walk up the inheritance chain
    char* current_type_name = type_info->type_name;
    char* current_parent_name = type_info->parent_type_name;

    // Check current type
    if (strcmp(current_type_name, target_type_name) == 0) {

        return 1;
    }
    
    // Check parent types (simplified - only one level for now)
    if (current_parent_name && strcmp(current_parent_name, target_type_name) == 0) {

        return 1;
    }
    
    // Special case: all types inherit from Object
    if (strcmp(target_type_name, "Object") == 0) {

        return 1;
    }
    

    return 0;
}

// For backward compatibility, redirect old function to new one
int __hulk_runtime_type_check(void* object, const char* target_type_name) {
    return __hulk_runtime_type_check_enhanced(object, target_type_name);
}

// Runtime error function for failed casts
void __hulk_runtime_error(const char* error_message) {
    if (error_message) {
        printf("%s", error_message);
    } else {
        printf("Runtime Error: Unknown error occurred\n");
    }
    exit(1);
}