#pragma once
#include <stdint.h>
namespace paper {
constexpr uint64_t HOTSPOT_WINDOW_MS = 60ULL * 60 * 1000;
inline bool hotspotWanted(bool always, uint64_t uptimeMs, unsigned clients) {
  return always || uptimeMs < HOTSPOT_WINDOW_MS || clients > 0;
}
inline uint32_t hotspotSecondsRemaining(uint64_t uptimeMs) {
  return uptimeMs >= HOTSPOT_WINDOW_MS ? 0 : (HOTSPOT_WINDOW_MS - uptimeMs + 999) / 1000;
}
}
