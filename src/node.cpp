#include "tree.hpp"
#include <sstream>
#include <iostream>
#include <error_handler.hpp>

Node::Node(const SourceLocation& loc) : location(loc) {}