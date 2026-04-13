#ifndef CLASSIC_H
#define CLASSIC_H

#include <stdint.h>
#include "keyboard.h"

struct keyboard* classic_init();
void clasic_keyboard_handle_interrupt(uint8_t scancode);

#endif