#pragma once
#include <stdint.h>
struct RadioSettings {
  float frequency = 0, bandwidth = 125, dutyPercent = 1;
  uint8_t sf = 7, cr = 5, sync = 0x12;
  int8_t power = 10;
  uint16_t preamble = 8;
  bool crc = true, tx = false;
};
inline bool validRadio(const RadioSettings& c) {
  return (c.frequency==0 || (c.frequency>=430 && c.frequency<=510) || (c.frequency>=863 && c.frequency<=928)) &&
    (c.bandwidth==125 || c.bandwidth==250 || c.bandwidth==500) && c.sf>=5 && c.sf<=12 && c.cr>=5 && c.cr<=8 &&
    c.power>=-9 && c.power<=22 && c.preamble>=8 && c.dutyPercent>=0.1f && c.dutyPercent<=100;
}
inline uint64_t txIntervalUs(uint64_t airtimeUs, float dutyPercent) {
  return uint64_t(double(airtimeUs)*100.0/dutyPercent + 0.999999);
}
