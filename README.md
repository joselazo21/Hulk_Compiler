# Hulk Compiler

A complete compiler implementation for the Hulk programming language, featuring object-oriented programming with functional elements.

## Features

- **Object-Oriented Programming**: Classes with inheritance
- **Type System**: Static and dynamic type checking with `is` and `as` operators
- **Control Structures**: Conditional expressions, loops, and iterators
- **LLVM Backend**: Optimized code generation through LLVM IR
- **Built-in Functions**: Mathematical operations, I/O, and string manipulation

## Quick Start

### Build
```bash
make compile
```

### Usage
```bash
make execute
```

## Example Code

```hulk
type Person(name, age) {
    name: String = name;
    age: Number = age;
    
    greet(): String => "Hello, I'm " @@ self.name;
}

let person = new Person("Alice", 25) in
    print(person.greet());
```

## Project Structure

- `src/` - Compiler source code
- `include/` - Header files
- `CMakeLists.txt` - Build configuration
- `informe_compilador.pdf` - Technical documentation (Spanish)

## Authors

- José Ernesto Morales Lazo
- Salma Fonseca Curbelo

---
*Universidad de La Habana - Facultad de Matemática y Computación*