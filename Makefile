# Makefile for HULK Compiler with Flex/Bison Parser
# Meets professors' requirements for project infrastructure

# Variables
BUILD_DIR = build
HULK_DIR = hulk
SCRIPT_FILE = script.hulk
COMPILER_EXECUTABLE = $(BUILD_DIR)/bin/main_flex_bison
COMPILED_PROGRAM = $(HULK_DIR)/hulk_program

# Default target
.PHONY: all compile execute clean help

# Help target
help:
	@echo "Available targets:"
	@echo "  compile  - Compile the HULK script and generate artifacts in hulk/ directory"
	@echo "  execute  - Execute the compiled HULK program"
	@echo "  clean    - Clean build artifacts"
	@echo "  help     - Show this help message"

# Compile target - generates hulk directory with artifacts
compile: $(COMPILED_PROGRAM)

$(COMPILED_PROGRAM): $(COMPILER_EXECUTABLE) $(SCRIPT_FILE)
	@echo "Processing HULK script with Flex/Bison parser..."
	@mkdir -p $(HULK_DIR)
	@# Check if script.hulk exists in root directory
	@if [ ! -f $(SCRIPT_FILE) ]; then \
		echo "Error: $(SCRIPT_FILE) not found in root directory"; \
		exit 1; \
	fi
	@# Run the Flex/Bison compiler directly with script.hulk file
	@cd $(BUILD_DIR)/bin && ./main_flex_bison ../../$(SCRIPT_FILE)
	@# Check if script.ll was generated (it should be in the root directory)
	@if [ -f script.ll ]; then \
		echo "LLVM IR generated successfully"; \
		cp script.ll $(HULK_DIR)/; \
		if [ -f ast.txt ]; then \
			cp ast.txt $(HULK_DIR)/; \
		fi; \
	else \
		echo "Error: LLVM IR not generated"; \
		exit 1; \
	fi
	@# Compile LLVM IR to object file
	@llc -relocation-model=pic -filetype=obj $(HULK_DIR)/script.ll -o $(HULK_DIR)/output.o
	@# Link with runtime libraries to create executable
	@clang $(HULK_DIR)/output.o -o $(COMPILED_PROGRAM) -lc -lm $(BUILD_DIR)/libruntime_iter.a $(BUILD_DIR)/libruntime_print.a
	@echo "Compilation complete. Artifacts stored in $(HULK_DIR)/ directory"

# Execute target - runs the compiled program (automatically compiles if needed)
execute: $(COMPILED_PROGRAM)
	@echo "Executing compiled HULK program..."
	@$(COMPILED_PROGRAM)

# Build the compiler executable (only when compiler source code changes)
$(COMPILER_EXECUTABLE):
	@echo "Building HULK Flex/Bison compiler..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && make main_flex_bison runtime_iter runtime_print
	@echo "Flex/Bison Compiler built successfully"

# Clean target
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(HULK_DIR)
	@rm -f script.ll ast.txt
	@echo "Clean complete"