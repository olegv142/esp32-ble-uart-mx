#pragma once

#include "util.h"

void fatal(const char* what);

#define BUG() do { fatal("BUG at line " LINE_STRING); } while (0)
#define BUG_ON(cond) do { if (cond) BUG(); } while (0)
