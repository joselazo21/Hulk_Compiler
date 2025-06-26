#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Enhanced TypeInfo structure matching LLVM struct
typedef struct TypeInfo {
    char* type_name;
    char* parent_type_name;  // Simplified - stores parent type name as string
} TypeInfo;

// Global type registry for inheritance chain lookup
#define MAX_TYPES 1000
typedef struct TypeRegistryEntry {
    char* type_name;
    char* parent_type_name;
} TypeRegistryEntry;

static TypeRegistryEntry type_registry[MAX_TYPES];
static int registry_size = 0;

// Function to register a type and its parent in the global registry
void __hulk_register_type(const char* type_name, const char* parent_type_name) {
    if (registry_size >= MAX_TYPES) {
        return; // Registry full
    }
    
    // Check if type is already registered
    for (int i = 0; i < registry_size; i++) {
        if (type_registry[i].type_name && strcmp(type_registry[i].type_name, type_name) == 0) {
            return; // Already registered
        }
    }
    
    // Allocate memory and copy strings
    type_registry[registry_size].type_name = malloc(strlen(type_name) + 1);
    strcpy(type_registry[registry_size].type_name, type_name);
    
    if (parent_type_name) {
        type_registry[registry_size].parent_type_name = malloc(strlen(parent_type_name) + 1);
        strcpy(type_registry[registry_size].parent_type_name, parent_type_name);
    } else {
        type_registry[registry_size].parent_type_name = NULL;
    }
    
    registry_size++;
}

// Function to find parent type in registry
const char* __hulk_get_parent_type(const char* type_name) {
    for (int i = 0; i < registry_size; i++) {
        if (type_registry[i].type_name && strcmp(type_registry[i].type_name, type_name) == 0) {
            return type_registry[i].parent_type_name;
        }
    }
    return NULL;
}

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
    
    // Special case: all types inherit from Object
    if (strcmp(target_type_name, "Object") == 0) {
        return 1;
    }
    
    // Walk up the inheritance chain
    const char* current_type_name = type_info->type_name;

    // Check current type
    if (strcmp(current_type_name, target_type_name) == 0) {
        return 1;
    }
    
    // Walk up the inheritance chain using the global registry
    const char* parent_name = __hulk_get_parent_type(current_type_name);
    
    // Traverse the inheritance chain
    while (parent_name && strcmp(parent_name, "Object") != 0) {
        if (strcmp(parent_name, target_type_name) == 0) {
            return 1;
        }
        parent_name = __hulk_get_parent_type(parent_name);
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