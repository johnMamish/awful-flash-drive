#include "char_buffer.h"

void char_buffer_init(volatile char_buffer_t *cb, uint8_t *space, uint32_t numel)
{
    cb->head = 0;
    cb->tail = 0;
    cb->data = space;
    cb->numel = numel;
}

int char_buffer_isempty(const volatile char_buffer_t *cb)
{
    return (cb->head == cb->tail);
}

int char_buffer_putc(volatile char_buffer_t *cb, uint8_t put)
{
    // check and see if adding a new character would result in overflow
    const uint32_t newhead = ((cb->head + 1) == cb->numel) ? 0 : (cb->head + 1);
    if (newhead != cb->tail) {
        cb->data[cb->head] = put;
        cb->head = newhead;
        return 0;
    } else {
        return 1;
    }
}

int char_buffer_getc(volatile char_buffer_t *cb, uint8_t *get)
{
    if (!char_buffer_isempty(cb)) {
        const uint32_t newtail = ((cb->tail + 1) == cb->numel) ? 0 : (cb->tail + 1);
        *get = cb->data[cb->tail];
        cb->tail = newtail;
        return 0;
    } else {
        return 1;
    }
}
