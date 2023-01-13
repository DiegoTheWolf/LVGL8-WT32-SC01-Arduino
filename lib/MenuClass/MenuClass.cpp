#include "MenuClass.h"

MenuClass::MenuClass()
{
    _level = 0;
    _option = 0;
    _maxOptions = 1;
}

void MenuClass::moveVertical(int count)
{
    _option += count;
    if (_option >= _maxOptions)
        _option = 0;
}

void MenuClass::moveDepth(int count)
{
    _level += count;
    _option = 0;
}

void MenuClass::setMenu(int level, int option, int maxOptions)
{
    _level = _level;
    _option = _option;
    _maxOptions = _maxOptions;
}
