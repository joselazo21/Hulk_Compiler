#pragma once

struct SourceLocation {
    int line;
    int column;
    
    SourceLocation(int l = 0, int c = 0) : line(l), column(c) {}
};