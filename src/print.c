#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int print(char* str) {
    return printf("%s\n", str);
}

// Concatenate two C strings, return a newly allocated string (caller must free)
char* string_concat(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    char* result = (char*)malloc(len_a + len_b + 1);
    if (!result) return NULL;
    memcpy(result, a, len_a);
    memcpy(result + len_a, b, len_b);
    result[len_a + len_b] = '\0';
    return result;
}

// Concatenate two C strings with a space in between, return a newly allocated string (caller must free)
char* string_concat_with_space(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    char* result = (char*)malloc(len_a + 1 + len_b + 1); // +1 for space, +1 for null terminator
    if (!result) return NULL;
    memcpy(result, a, len_a);
    result[len_a] = ' '; // Add space
    memcpy(result + len_a + 1, b, len_b);
    result[len_a + 1 + len_b] = '\0';
    return result;
}
