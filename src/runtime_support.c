#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int __hulk_runtime_type_check(void* object, const char* target_type_name) {
    if (!object || !target_type_name) {
        return 0;
    }
    
    // The first field of every object is a pointer to its type name string
    char** type_id_ptr = (char**)object;
    char* actual_type_name = *type_id_ptr;
    
    if (!actual_type_name) {
        return 0;
    }
    
    // Direct type match
    if (strcmp(actual_type_name, target_type_name) == 0) {
        return 1;
    }
    
    if (strcmp(target_type_name, "Object") == 0) {
        return 1;
    }
    
    return 0;
}

// Runtime error function for failed casts
void __hulk_runtime_error(const char* error_message) {
    if (error_message) {
        printf("Runtime Error: %s", error_message);
    } else {
        printf("Runtime Error: Unknown error occurred\n");
    }
    exit(1);
}
typedef struct TypeInfo {
    char* type_name;
    struct TypeInfo* parent_type;
} TypeInfo;

typedef struct RuntimeObject {
    TypeInfo* type_info;
    // Object data follows...
} RuntimeObject;

int __hulk_runtime_type_check_enhanced(void* object, const char* target_type_name) {
    if (!object || !target_type_name) {
        return 0;
    }
    
    RuntimeObject* obj = (RuntimeObject*)object;
    if (!obj->type_info) {
        return 0;
    }
    
    // Walk up the inheritance chain
    TypeInfo* current_type = obj->type_info;
    while (current_type) {
        if (strcmp(current_type->type_name, target_type_name) == 0) {
            return 1;
        }
        current_type = current_type->parent_type;
    }
    
    return 0;
}