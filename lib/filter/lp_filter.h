#pragma once

#include <Arduino.h>

#define ORDER 2

class LowPassFilter {
 private:
  float a[ORDER];
  float b[ORDER + 1];
  float omega0;
  float dt;
  bool adapt;
  float tn1 = 0;
  float x[ORDER + 1];  // Raw values
  float y[ORDER + 1];  // Filtered values

  void setCoef();

 public:
  void begin(float f0);

  float filter(float xn);
};
