#include "xframe.h"
#include "mx_config.h"
#include "checksum.h"
#include "mx_encoding.h"

struct err_count bad_chunks;
struct err_count skip_chunks;

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
