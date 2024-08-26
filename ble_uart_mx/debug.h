#pragma once

#include "util.h"

void fatal(const char* what);

#define BUG_ON(cond) do { if (cond) fatal("BUG at " LINE_STRING); } while (0)
