#pragma once

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x

#define STRZ_LEN(s) (sizeof(s)-1)
