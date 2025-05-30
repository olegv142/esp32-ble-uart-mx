#include "xframe.h"
#include "mx_config.h"
#include "checksum.h"
#include "mx_encoding.h"

struct err_count bad_chunks;
struct err_count skip_chunks;

bool transmit_xframe(
    uint8_t const* tx_data, size_t len, uint8_t binary,
    uint8_t* (*get_chunk)(size_t sz, void* ctx),
    bool (*tx_chunk)(uint8_t* chunk, size_t sz, void* ctx),
    void* ctx
  )
{
  static uint8_t  last_frame_sn;
  static uint8_t  last_chunk_sn;
  static uint32_t last_chksum;
  uint8_t tx_sn = last_frame_sn;
  // The following flag is set in case previous transmission was incomplete
  // so we will skip already transmitted frames
  bool skip = (last_chunk_sn != last_frame_sn);
  if (!skip)
      last_chksum = CHKSUM_INI;
  uint8_t first = 1, last;
  while (len) {
    size_t chunk = len;
    if (!(last = (chunk <= MAX_CHUNK)))
      chunk = MAX_CHUNK;
    tx_sn += 1;
    if (!skip) {
      uint8_t* const chunk_buff = get_chunk(XHDR_SIZE + chunk + CHKSUM_SIZE, ctx);
      if (!chunk_buff)
        return false;
      uint8_t const chunk_hdr = mk_xframe_hdr(tx_sn, binary, first, last);
      chunk_buff[0] = chunk_hdr;
      uint32_t const chksum = chksum_copy(tx_data, chunk, chunk_buff + 1, chksum_up(chunk_hdr, last_chksum));
      if (!tx_chunk(chunk_buff, XHDR_SIZE + chunk + CHKSUM_SIZE, ctx))
        return false;
      last_chksum = chksum;
      last_chunk_sn = tx_sn;
    } else if (last_chunk_sn == tx_sn)
      skip = false;
    first = 0;
    tx_data += chunk;
    len -= chunk;
  }
  last_frame_sn = tx_sn;
  return true;
}

void XFrameReceiver::receive(struct data_chunk const* chunk)
{
  uint8_t h;
  uint32_t chksum;
  if (chunk->len <= XHDR_SIZE + CHKSUM_SIZE || chunk->len > MAX_SIZE) {
#ifndef VERBOSE_DEBUG
    uart_debug_begin();
    uart_print_strz("invalid chunk size from [");
    uart_print(m_tag);
    uart_print_strz("]");
    uart_end();
#endif
    ++bad_chunks.cnt;
    goto out;
  }
  h = chunk->data[0];
  if (!(h & XH_FIRST)) {
    if (m_last_chunk < 0)
      goto out_skip;
    if (m_next_sn != (h & XH_SN_MASK))
      goto out_skip;
    if (m_last_chunk + 1 >= MAX_CHUNKS)
      goto out_skip;
  }
  chksum = h & XH_FIRST ? CHKSUM_INI : m_last_chksum;
  if (!chksum_validate(chunk->data, chunk->len - CHKSUM_SIZE, &chksum)) {
#ifndef VERBOSE_DEBUG
    uart_debug_begin();
    uart_print_strz("invalid checksum from [");
    uart_print(m_tag);
    uart_print_strz("]");
    uart_end();
#endif
    ++bad_chunks.cnt;
    goto out;
  }
  if (h & XH_FIRST)
    reset();
  m_chunks[++m_last_chunk] = *chunk;
  m_next_sn = (h + 1) & XH_SN_MASK;
  m_last_chksum = chksum;
  if (h & XH_LAST)
    flush();
  return;
out_skip:
  ++skip_chunks.cnt;
#ifdef VERBOSE_DEBUG
  uart_debug_begin();
  uart_print_strz("skip chunk from [");
  uart_print(m_tag);
  uart_print_strz("]");
  uart_end();
#endif
out:
  free(chunk->data);
}

void XFrameReceiver::reset(void)
{
  for (int i = 0; i <= m_last_chunk; ++i)
    free(m_chunks[i].data);
  m_last_chunk = -1;
}

void XFrameReceiver::flush(void)
{
  uint8_t const is_binary = m_chunks[0].data[0] & XH_BINARY;
  char enc_buff[MAX_ENCODED_CHUNK_LEN];
  uart_begin();
#ifndef SIMPLE_LINK
  uart_print(m_tag);
#endif
  if (is_binary)
    uart_print(ENCODED_DATA_START_TAG);
  for (int i = 0; i <= m_last_chunk; ++i) {
    size_t len = m_chunks[i].len - XHDR_SIZE - CHKSUM_SIZE;
    const uint8_t* const pchunk = m_chunks[i].data + XHDR_SIZE;
    const char* out_data = (const char*)pchunk;
    if (is_binary) {
      len = encode(pchunk, len, enc_buff);
      out_data = enc_buff;
    }
    uart_write(out_data, len);
  }
  uart_end();
  reset();
}
