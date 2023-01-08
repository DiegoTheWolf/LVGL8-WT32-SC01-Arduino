#include "helper.h"

void erasePreferences() {
  nvs_flash_erase();  // erase the NVS partition and...
  nvs_flash_init();   // initialize the NVS partition.
}

void createPreferences(Preferences pref) {
  pref.begin("scale", false);
  pref.clear();
  pref.putFloat("sfactor", 0.9F);
  pref.putLong("zeropoint", 1);
  pref.end();
}