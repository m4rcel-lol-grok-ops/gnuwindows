/*
 * VGA text mode 80x25 @ 0xB8000 — identity-mapped by VMM.
 */

#include <gw/vga.h>
#include <stdint.h>
#include <stddef.h>

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ATTR 0x0F /* white on black */
#define VGA_MEM  ((volatile uint16_t *)0xB8000)

static int row, col;

static void scroll(void) {
    for (int r = 1; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            VGA_MEM[(r - 1) * VGA_COLS + c] = VGA_MEM[r * VGA_COLS + c];
    for (int c = 0; c < VGA_COLS; c++)
        VGA_MEM[(VGA_ROWS - 1) * VGA_COLS + c] = (uint16_t)((VGA_ATTR << 8) | ' ');
    if (row > 0) row = VGA_ROWS - 1;
}

void vga_clear(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_MEM[i] = (uint16_t)((VGA_ATTR << 8) | ' ');
    row = 0;
    col = 0;
}

void vga_init(void) {
    vga_clear();
}

void vga_putc(char c) {
    if (c == '\n') {
        col = 0;
        row++;
        if (row >= VGA_ROWS) scroll();
        return;
    }
    if (c == '\r') {
        col = 0;
        return;
    }
    if (c == '\t') {
        col = (col + 4) & ~3;
        if (col >= VGA_COLS) {
            col = 0;
            row++;
            if (row >= VGA_ROWS) scroll();
        }
        return;
    }
    if (c == '\b') {
        if (col > 0) {
            col--;
            VGA_MEM[row * VGA_COLS + col] = (uint16_t)((VGA_ATTR << 8) | ' ');
        }
        return;
    }
    if (c < 32) return;

    VGA_MEM[row * VGA_COLS + col] = (uint16_t)((VGA_ATTR << 8) | (uint8_t)c);
    col++;
    if (col >= VGA_COLS) {
        col = 0;
        row++;
        if (row >= VGA_ROWS) scroll();
    }
}

void vga_write(const char *s) {
    if (!s) return;
    while (*s) vga_putc(*s++);
}
