#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "nvs_flash.h"

void erasePreferences();

void createPreferences(Preferences pref);