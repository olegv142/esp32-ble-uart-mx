#pragma once

#define LINE_STRING STRINGIZE(__LINE__)

#define BUG() do { fatal("BUG at line " LINE_STRING); } while (0)
#define BUG_ON(cond) do { if (cond) BUG(); } while (0)

void reset_self(void);
void fatal(const char* what);
