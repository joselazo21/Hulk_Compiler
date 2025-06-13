#ifndef PRINT_H
#define PRINT_H

#ifdef __cplusplus
extern "C" {
#endif

int print(char* str);
char* string_concat(const char* a, const char* b);
char* string_concat_with_space(const char* a, const char* b);

#ifdef __cplusplus
}
#endif

#endif // PRINT_H
