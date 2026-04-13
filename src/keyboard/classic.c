#include "classic.h"
#include "keyboard.h"
#include "io/io.h"
#include <stdint.h>
#include <stddef.h>

int classic_keyboard_init();

static uint8_t keyboard_scan_set_one[] = {
    0x00, 0x1B, '1', '2', '3', '4', '5',
    '6', '7', '8', '9', '0', '-', '=',
    0x08, '\t', 'Q', 'W', 'E', 'R', 'T',
    'Y', 'U', 'I', 'O', 'P', '[', ']',
    0x0d, 0x00, 'A', 'S', 'D', 'F', 'G',
    'H', 'J', 'K', 'L', ';', '\'', '`',
    0x00, '\\', 'Z', 'X', 'C', 'V', 'B',
    'N', 'M', ',', '.', '/', 0x00, '*',
    0x00, 0x20, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, '7', '8', '9', '-', '4', '5',
    '6', '+', '1', '2', '3', '0', '.'
};

struct keyboard classic_keyboard = {
    .name = {"Classic"},
    .init = classic_keyboard_init
};

int classic_keyboard_init()
{
    outb(0x64, 0xAE);   // enable first PS/2 port
    return 0;
}

uint8_t classic_keyboard_scancode_to_char(uint8_t scancode)
{
    size_t size_of_keyboard_set_one =
        sizeof(keyboard_scan_set_one) / sizeof(uint8_t);
    if (scancode > size_of_keyboard_set_one)
        return 0;
    return keyboard_scan_set_one[scancode];
}

void clasic_keyboard_handle_interrupt(uint8_t scancode)
{
    // Ignore key-release events (high bit set)
    if (scancode & 0x80)
        return;

    char c = classic_keyboard_scancode_to_char(scancode);
    if (c == 0)
        return;

    // Convert uppercase to lowercase
    if (c >= 'A' && c <= 'Z')
        c = c + 32;

    keyboard_push(c);
}

struct keyboard* classic_init()
{
    return &classic_keyboard;
}