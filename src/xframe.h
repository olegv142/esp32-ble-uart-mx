#pragma once

/*
 * Extended frames related definitions
 */

#include "mx_types.h"
#include "mx_uart.h"

/*
 * Frame header bits
 */

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

/*
 * Transmits extended frame by breaking it into fragments that fit within the MTU
 */
bool transmit_xframe(
    uint8_t const* tx_data, size_t len, uint8_t binary,
    uint8_t* (*get_chunk)(size_t sz, void* ctx),
    bool (*tx_chunk)(uint8_t* chunk, size_t sz, void* ctx),
    void* ctx
  );

/*
 * Frame receiver class. It validates and combines chunks reconstructing the data frame.
 */
class XFrameReceiver {
public:
  XFrameReceiver(char tag)
    : m_tag(tag), m_next_sn(0), m_last_chunk(-1) {}

  void receive(struct data_chunk const* chunk);
  void reset(void);

protected:
  virtual void frame_start(bool binary) = 0;
  virtual void chunk_output(const uint8_t* data, size_t len) = 0;
  virtual void frame_end() = 0;

  void flush(void);

  char const m_tag;
  uint8_t    m_next_sn;
  int        m_last_chunk;
  uint32_t   m_last_chksum;
  struct data_chunk m_chunks[MAX_CHUNKS];
};

class XFrameReceiverToUart : public XFrameReceiver {
public:
  XFrameReceiverToUart(char tag) : XFrameReceiver(tag) {}
  
protected:
  virtual void frame_start(bool binary);
  virtual void chunk_output(const uint8_t* data, size_t len);
  virtual void frame_end();
};

extern struct err_count bad_chunks;
extern struct err_count skip_chunks;
