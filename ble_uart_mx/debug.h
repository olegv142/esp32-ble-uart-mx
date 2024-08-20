#pragma once

void fatal(const char* what);

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STRING STRINGIZE(__LINE__)

#define BUG_ON(cond) do { if (cond) fatal("BUG at " LINE_STRING); } while (0)
