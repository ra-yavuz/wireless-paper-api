#include "device_policy.h"
#include "radio_settings.h"
#include <assert.h>
#include <stdio.h>
int main() {
  using namespace paper;
  assert(HOTSPOT_WINDOW_MS==3600000);
  assert(hotspotWanted(false,0,0));
  assert(hotspotWanted(false,HOTSPOT_WINDOW_MS-1,0));
  assert(!hotspotWanted(false,HOTSPOT_WINDOW_MS,0));
  assert(hotspotWanted(false,HOTSPOT_WINDOW_MS,1));
  assert(hotspotWanted(false,HOTSPOT_WINDOW_MS+86400000,2));
  assert(!hotspotWanted(false,HOTSPOT_WINDOW_MS+86400000,0));
  assert(hotspotWanted(true,UINT64_MAX,0));
  assert(!hotspotWanted(false,uint64_t(UINT32_MAX)+100000,0));
  assert(hotspotSecondsRemaining(0)==3600);
  assert(hotspotSecondsRemaining(HOTSPOT_WINDOW_MS-1)==1);
  assert(hotspotSecondsRemaining(HOTSPOT_WINDOW_MS)==0);
  RadioSettings c;assert(validRadio(c));
  c.enabled=true;assert(!validRadio(c));c.enabled=false;
  c.frequency=868.1;c.bandwidth=250;c.sf=12;c.cr=8;c.power=22;c.preamble=65535;c.tx=true;
  assert(validRadio(c));c.frequency=700;assert(!validRadio(c));c.frequency=915;
  c.dutyPercent=0;assert(!validRadio(c));c.dutyPercent=101;assert(!validRadio(c));c.dutyPercent=1;
  c.sf=0;assert(!validRadio(c));c.sf=7;c.bandwidth=17;assert(!validRadio(c));
  assert(txIntervalUs(100000,1)==10000000);
  assert(txIntervalUs(100000,100)==100000);
  assert(txIntervalUs(100000,0.5)==20000000);
  puts("Policy tests passed: exact hour boundary, client hold, last disconnect, permanent mode, long uptime, modem limits, airtime pacing.");
}
