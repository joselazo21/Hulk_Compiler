#include <stdlib.h>
#include <stdint.h>
#include <math.h>

// Define struct range (sin typedef)
struct range {
    int32_t current;
    int32_t end;
};

// Crea un nuevo iterador range
struct range* range_new(int32_t start, int32_t end) {
    struct range* r = (struct range*)malloc(sizeof(struct range));
    r->current = start;
    r->end = end;
    return r;
}

// Note: range function is now implemented in LLVM IR, so we don't define it here

// Devuelve 1 si hay siguiente, 0 si terminó
int32_t __iter_x_next(struct range* r) {
    if (r->current < r->end) {
        return 1;
    }
    return 0;
}

// Devuelve el valor actual y avanza el iterador
int32_t __iter_x_current(struct range* r) {
    int32_t val = r->current;
    r->current++;
    return val;
}

// Alias para que coincida con los nombres generados por LLVM (por si acaso)
int32_t __iter_x_next_void(void* r) {
    return __iter_x_next((struct range*)r);
}
int32_t __iter_x_current_void(void* r) {
    return __iter_x_current((struct range*)r);
}

// Alias para símbolos con punto, usando asm para GNU/Clang
// Esto hace que __iter_x.next y __iter_x.current sean alias de __iter_x_next y __iter_x_current

__attribute__((used)) int32_t __iter_x_next_alias(struct range* r) __asm__("__iter_x.next");
__attribute__((used)) int32_t __iter_x_next_alias(struct range* r) { 
    if (!r) return 0; // Protección contra puntero nulo
    return __iter_x_next(r); 
}

__attribute__((used)) int32_t __iter_x_current_alias(struct range* r) __asm__("__iter_x.current");
__attribute__((used)) int32_t __iter_x_current_alias(struct range* r) { 
    if (!r) return 0; // Protección contra puntero nulo
    return __iter_x_current(r); 
}

// Math functions implementation
int32_t sqrt_i32(int32_t x) {
    if (x < 0) return 0; // Handle negative input
    return (int32_t)sqrt((double)x);
}

int32_t sin_i32(int32_t x) {
    // Assuming x is in degrees, convert to radians and scale result by 1000
    double radians = ((double)x) * M_PI / 180.0;
    return (int32_t)(sin(radians) * 1000.0);
}

int32_t cos_i32(int32_t x) {
    // Assuming x is in degrees, convert to radians and scale result by 1000
    double radians = ((double)x) * M_PI / 180.0;
    return (int32_t)(cos(radians) * 1000.0);
}
