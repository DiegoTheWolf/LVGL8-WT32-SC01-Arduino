#pragma once

#include <Arduino.h>

class MenuClass
{
public:
    MenuClass();
    int _maxOptions;
    int _level;
    int _option;
    void moveVertical(int count);
    void moveDepth(int count);
    void setMenu(int level = 0, int option = 0, int maxOptions = 1);
};