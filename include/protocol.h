#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace paper {
#ifdef WIRELESS_TRACKER
constexpr size_t WIDTH = 160, HEIGHT = 80;
#else
constexpr size_t WIDTH = 250, HEIGHT = 122;
#endif
constexpr size_t STRIDE = (WIDTH + 7) / 8;
constexpr size_t FRAME_SIZE = STRIDE * HEIGHT, MAX_BODY = 8192, CHUNK = 224;
inline uint16_t u16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t* p) { return u16(p) | uint32_t(u16(p + 2)) << 16; }
inline uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t c = ~0u;
  while (n--) { c ^= *p++; for (int i = 0; i < 8; ++i) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1))); }
  return ~c;
}
enum class Result { Pending, Complete, Duplicate, Invalid, Busy };
class Assembler {
 public:
  uint8_t body[MAX_BODY]{};
  uint16_t total = 0;
  uint8_t kind = 0;
  Result add(const uint8_t* p, size_t n, uint32_t now) {
    if (n < 17 || n > 16 + CHUNK || p[0] != 'W' || p[1] != 'P' || p[2] != 1 || (p[3] != 1 && p[3] != 2)) return Result::Invalid;
    uint32_t incoming = u32(p + 4), checksum = u32(p + 12);
    uint16_t length = u16(p + 8), offset = u16(p + 10);
    if (!length || length > MAX_BODY || (p[3] == 2 && length != FRAME_SIZE) || offset >= length || offset % CHUNK) return Result::Invalid;
    size_t remaining = size_t(length - offset);
    size_t expected = remaining < CHUNK ? remaining : CHUNK;
    if (n - 16 != expected) return Result::Invalid;
    for (int i = 0; i < 8; ++i) if (historyValid[i] && history[i] == incoming) return Result::Duplicate;
    if (active && uint32_t(now - touched) > 120000) active = false;
    if (active && incoming != id) return Result::Busy;
    if (!active) {
      active = true; id = incoming; total = length; kind = p[3]; crc = checksum; count = 0;
      memset(seen, 0, sizeof(seen));
    }
    if (length != total || p[3] != kind || checksum != crc) return Result::Invalid;
    touched = now;
    size_t index = offset / CHUNK;
    if (seen[index]) return memcmp(body + offset, p + 16, expected) == 0 ? Result::Duplicate : Result::Invalid;
    memcpy(body + offset, p + 16, expected); seen[index] = true; count += expected;
    if (count != total) return Result::Pending;
    active = false;
    if (crc32(body, total) != crc) return Result::Invalid;
    // Remember only after the consumer accepts the completed command.
    return Result::Complete;
  }
  void accept() { history[historyIndex] = id; historyValid[historyIndex] = true; historyIndex = (historyIndex + 1) % 8; }
 private:
  uint32_t id = 0, crc = 0, touched = 0, history[8]{};
  size_t count = 0, historyIndex = 0;
  bool active = false, seen[(MAX_BODY + CHUNK - 1) / CHUNK]{}, historyValid[8]{};
};
}
