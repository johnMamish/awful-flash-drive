/**
 * John Mamish is the sole copyright owner of this file.
 *
 * Licensed under GPL3
 */

#include <stdint.h>

typedef struct char_buffer
{
    uint8_t *data;
    uint32_t numel;
    uint32_t head;
    uint32_t tail;
} char_buffer_t;

void char_buffer_init(volatile char_buffer_t *cb, uint8_t *space, uint32_t numel);

int char_buffer_isempty(const volatile char_buffer_t *cb);

int char_buffer_putc(volatile char_buffer_t *cb, uint8_t put);

int char_buffer_getc(volatile char_buffer_t *cb, uint8_t *get);
