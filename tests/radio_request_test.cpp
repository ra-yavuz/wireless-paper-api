#include "radio_request.h"
#include <assert.h>
#include <stdio.h>
RadioSettings active;
int calls=0;
bool init(float frequency) { ++calls; assert(active.frequency==frequency); return frequency!=900; }
void earlyExit(const RadioSettings& overrides) {
  RadioRequestProfile scope(active,true,init);
  assert(scope.apply(overrides));
  assert(active.frequency==overrides.frequency);
  // Models any return path after applying the request's hardware profile.
}
int main() {
  active.frequency=868.1;active.enabled=true;
  RadioSettings defaults=active,overrides=active;
  overrides.frequency=915;overrides.sf=10;overrides.bandwidth=250;
  earlyExit(overrides);
  assert(calls==2 && active.frequency==defaults.frequency && active.sf==defaults.sf);
  {
    RadioRequestProfile scope(active,true,init);
    overrides.frequency=900;assert(!scope.apply(overrides));
    scope.restore();assert(scope.restored && active.frequency==defaults.frequency);
    int done=calls;scope.restore();assert(calls==done);
  }
  {
    int before=calls;
    RadioRequestProfile scope(active,false,init);
    assert(scope.apply(defaults));scope.restore();assert(calls==before);
  }
  active=RadioSettings();overrides.frequency=915;
  earlyExit(overrides);assert(active.frequency==0 && !active.enabled && !active.tx);
  active.frequency=900;active.enabled=true;
  {
    RadioRequestProfile scope(active,true,init);
    assert(scope.apply(overrides));scope.restore();
    assert(!scope.restored && active.frequency==900);
  }
  puts("Request radio profile tests passed: temporary settings, early return, failed initialization, idempotent restore, inactive baseline, persistent-profile no-op.");
}
