#pragma once

#include <Arduino.h>

#include "freertos/task.h"
#include "lp_filter.h"

class HX711
{
private:
  byte PD_SCK;                    // Power Down and Serial Clock Input Pin
  byte DOUT;                      // Serial Data Output Pin
  byte GAIN;                      // amplification factor
  float TARE_OFFSET = 0;          // used for tare weight
  float ZEROPOINT_OFFSET_CAL = 0; // used for the basic zero point deviation of a sensor (calibrated)
  float SCALE_CAL = 1;            // used to return weight in grams, kg, ounces, whatever (calibrated)
  float CURRENTREADING = 0;       // saves the last reading, to calculate weights.
  static const int lastReadingsCount = 20;
  float LASTREADINGS[lastReadingsCount];
  int lastReadingIndex = 0;
  long RAWREADING = 0; // raw reading without filter
  const float CUTOFFFREQ = 2;
  LowPassFilter lpFilter;

  // Wait for the HX711 to become ready
  void wait_ready();

public:
  HX711();

  virtual ~HX711();

  // waits for the chip to be ready and returns a reading
  float read();

  // Check if HX711 is ready
  // from the datasheet:
  // When output data is not ready for retrieval, digital output pin DOUT is high. Serial clock
  // input PD_SCK should be low. When DOUT goes to low, it indicates data is ready for retrieval.
  bool is_ready();

  // Initialize library with data output pin, clock input pin and gain factor.
  // Channel selection is made by passing the appropriate gain:
  // - With a gain factor of 64 or 128, channel A is selected
  // - With a gain factor of 32, channel B is selected
  // The library default is "128" (Channel A).
  void begin(byte dout, byte pd_sck, byte gain = 128);

  // set the gain factor; takes effect only after a call to read()
  // channel A can be set for a 128 or 64 gain; channel B has a fixed 32 gain
  // depending on the parameter, the channel is also set to either A or B
  void set_gain(byte gain = 128);

  float get_last_reading();

  float get_last_reading_zeroed();

  float get_lastreadings_avg();

  float get_cal_force();

  float get_tare_force();

  long get_raw_reading();

  // set the OFFSET value for tare weight; times = how many times to read the tare value
  void tare(byte times = 10);

  // set the SCALE value; this value is used to convert the raw data to "human readable" data (measure units)
  void set_scale(float scale = 1.f);

  // set the SCALE value; based on the current measurements and the KNOWN FORCE
  void set_scale_current(float force);

  // get the current SCALE
  float get_scale();

  // set OFFSET, the value that's subtracted from the actual reading (tare weight)
  void set_tare_offset(float offset = 0);

  // get the current OFFSET
  float get_tare_offset();

  // set BASEOFFSET, the value that's subtracted from the actual reading (base tare weight)
  void set_zeropoint_offset(float zeropoint_offset);

  // set BASEOFFSET, based on the CURRENT measurements
  void set_zeropoint_offset_current();

  // get the current BASEOFFSET
  float get_zeropoint_offset();

  // puts the chip into power down mode
  void power_down();

  // wakes up the chip after power down mode
  void power_up();
};