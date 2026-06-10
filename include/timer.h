#ifndef TIMER_H
#define TIMER_H

#define timer_busy_wait(unit, n)\
    do {\
        uint64_t _end = timer_now(unit) + n;\
        while(timer_now(unit) < _end);\
    } while(0)

#include <stdint.h>

typedef enum {
    TIMER_SEC,
    TIMER_MS,
    TIMER_US,
    TIMER_NS
} timer_unit_t;

uint64_t timer_now(timer_unit_t unit);

#endif
