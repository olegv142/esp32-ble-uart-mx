#pragma once

//
// Check COND is true at compile time
//

#define STATIC_ASSERT2(COND, MSG) typedef char static_assertion_##MSG[2*(!!(COND))-1]
#define STATIC_ASSERT(COND) STATIC_ASSERT2(COND, _)
