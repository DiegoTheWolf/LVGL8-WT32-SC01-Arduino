#include "hx711_zp.h"

// Make shiftIn() be aware of clockspeed for ESP32
// See also:
// - https://github.com/bogde/HX711/issues/75
// - https://github.com/arduino/Arduino/issues/6561

uint8_t shiftInSlow(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder)
{
  uint8_t value = 0;
  uint8_t i;

  for (i = 0; i < 8; ++i)
  {
    digitalWrite(clockPin, HIGH);
    delayMicroseconds(1);
    if (bitOrder == LSBFIRST)
      value |= digitalRead(dataPin) << i;
    else
      value |= digitalRead(dataPin) << (7 - i);
    digitalWrite(clockPin, LOW);
    delayMicroseconds(1);
  }
  return value;
}

HX711::HX711()
{
  lpFilter.begin(CUTOFFFREQ);
}

HX711::~HX711()
{
}

void HX711::begin(byte dout, byte pd_sck, byte gain)
{
  PD_SCK = pd_sck;
  DOUT = dout;

  pinMode(PD_SCK, OUTPUT);
  pinMode(DOUT, INPUT_PULLUP);

  set_gain(gain);
}

bool HX711::is_ready()
{
  return digitalRead(DOUT) == LOW;
}

void HX711::set_gain(byte gain)
{
  switch (gain)
  {
  case 128: // channel A, gain factor 128
    GAIN = 1;
    break;
  case 64: // channel A, gain factor 64
    GAIN = 3;
    break;
  case 32: // channel B, gain factor 32
    GAIN = 2;
    break;
  }
}

float HX711::read()
{
  // Wait for the chip to become ready.
  wait_ready();

  // Define structures for reading data into.
  uint8_t data[3] = {0};

  // Protect the read sequence from system interrupts.  If an interrupt occurs during
  // the time the PD_SCK signal is high it will stretch the length of the clock pulse.
  // If the total pulse time exceeds 60 uSec this will cause the HX711 to enter
  // power down mode during the middle of the read sequence.  While the device will
  // wake up when PD_SCK goes low again, the reset starts a new conversion cycle which
  // forces DOUT high until that cycle is completed.
  //
  // The result is that all subsequent bits read by shiftIn() will read back as 1,
  // corrupting the value returned by read().

  // Begin of critical section.
  // Critical sections are used as a valid protection method
  // against simultaneous access in vanilla FreeRTOS.
  // Disable the scheduler and call portDISABLE_INTERRUPTS. This prevents
  // context switches and servicing of ISRs during a critical section.
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&mux);
  noInterrupts();

  // Pulse the clock pin 24 times to read the data.
  data[2] = shiftInSlow(DOUT, PD_SCK, MSBFIRST);
  data[1] = shiftInSlow(DOUT, PD_SCK, MSBFIRST);
  data[0] = shiftInSlow(DOUT, PD_SCK, MSBFIRST);

  // Set the channel and the gain factor for the next reading using the clock pin.
  for (unsigned int i = 0; i < GAIN; i++)
  {
    digitalWrite(PD_SCK, HIGH);
    delayMicroseconds(1);
    digitalWrite(PD_SCK, LOW);
    delayMicroseconds(1);
  }

  // End of critical section.
  interrupts();
  portEXIT_CRITICAL(&mux);

  // Replicate the most significant bit to pad out a 32-bit signed integer
  uint8_t filler = 0x00;
  if (data[2] & 0x80)
  {
    filler = 0xFF;
  }

  // Construct a 32-bit signed integer
  unsigned long value = (static_cast<unsigned long>(filler) << 24 | static_cast<unsigned long>(data[2]) << 16 | static_cast<unsigned long>(data[1]) << 8 | static_cast<unsigned long>(data[0]));

  RAWREADING = static_cast<long>(value);
  CURRENTREADING = lpFilter.filter((float)RAWREADING);

  lastReadingIndex++;
  if (lastReadingIndex >= lastReadingsCount)
    lastReadingIndex = 0;
  LASTREADINGS[lastReadingIndex] = CURRENTREADING;

  return CURRENTREADING;
}

long HX711::get_raw_reading()
{
  return RAWREADING;
}

// private
void HX711::wait_ready()
{
  // Wait for the chip to become ready.
  while (!is_ready())
  {
    vTaskDelay(10);
  }
}

float HX711::get_last_reading()
{
  return CURRENTREADING;
}

float HX711::get_last_reading_zeroed()
{
  return CURRENTREADING - ZEROPOINT_OFFSET_CAL;
}

float HX711::get_cal_force()
{
  return (float)(CURRENTREADING - ZEROPOINT_OFFSET_CAL) / SCALE_CAL;
}

float HX711::get_tare_force()
{
  return get_cal_force() - TARE_OFFSET;
}

void HX711::tare(byte avgTimes)
{
  read();
  set_tare_offset(get_cal_force());
}

void HX711::set_scale_current(float force)
{
  SCALE_CAL = lastreadings_avg() / force;
}

void HX711::set_scale(float scale)
{
  SCALE_CAL = scale;
}

float HX711::get_scale()
{
  return SCALE_CAL;
}

void HX711::set_tare_offset(float offset)
{
  TARE_OFFSET = offset;
}

float HX711::get_tare_offset()
{
  return TARE_OFFSET;
}

void HX711::set_zeropoint_offset_current()
{
  ZEROPOINT_OFFSET_CAL = lastreadings_avg();
}

void HX711::set_zeropoint_offset(float zeropoint_offset)
{
  ZEROPOINT_OFFSET_CAL = zeropoint_offset;
}

float HX711::get_zeropoint_offset()
{
  return ZEROPOINT_OFFSET_CAL;
}

void HX711::power_down()
{
  digitalWrite(PD_SCK, LOW);
  digitalWrite(PD_SCK, HIGH);
}

void HX711::power_up()
{
  digitalWrite(PD_SCK, LOW);
}

// private
float HX711::lastreadings_avg()
{
  float sum = 0;
  for (int i = 0; i < lastReadingsCount; i++)
  {
    sum += LASTREADINGS[i];
  }
  return sum / lastReadingsCount;
}