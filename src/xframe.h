#pragma once
//
// Extended frame chunk header related definitions
//

#define XH_BINARY_BIT 5
#define XH_FIRST_BIT  6
#define XH_LAST_BIT   7

#define XH_BINARY    (1<<XH_BINARY_BIT)
#define XH_FIRST     (1<<XH_FIRST_BIT)
#define XH_LAST      (1<<XH_LAST_BIT)

#define XH_SN_BITS    XH_BINARY_BIT
#define XH_SN_MASK   ((1<<XH_SN_BITS)-1)

static inline uint8_t mk_xframe_hdr(uint8_t sn, uint8_t binary, uint8_t first, uint8_t last)
{
  return (sn & XH_SN_MASK) | (binary << XH_BINARY_BIT) | (first << XH_FIRST_BIT) | (last << XH_LAST_BIT);
}
