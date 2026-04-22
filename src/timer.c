#include "../include/timer.h"
#include <time.h>

uint64_t timer_now(timer_unit_t unit) {

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

    switch(unit) {
        case TIMER_SEC:
            return (uint64_t)ts.tv_sec;

        case TIMER_MS:
            return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);

        case TIMER_US:
            return ((uint64_t)ts.tv_sec * 1000000ULL) + ((uint64_t)ts.tv_nsec / 1000ULL);

        case TIMER_NS:
            return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;

        default:
            return 0;
    }
}
