#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

static std::string getIteratorGlobalName(const std::string& iterName) {
    // "__iter_x" -> "__current_range_x"
    if (iterName.rfind("__iter_", 0) == 0) {
        return "__current_range_" + iterName.substr(7);
    }
    return "__current_range";
}

static std::string getIterNextFunctionName(const std::string& iterName) {
    return iterName + ".next";
}

static std::string getIterCurrentFunctionName(const std::string& iterName) {
    return iterName + ".current";
}


void printIndent(int depth) {
    for (int i = 0; i < depth; ++i) {
        std::cout << "│   ";
    }
}
