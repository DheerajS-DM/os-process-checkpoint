#include "common.h"

int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

int64_t elapsed_ns(int64_t start_ns) {
    return now_ns() - start_ns;
}

void timespec_add_ms(struct timespec *ts, int ms) {
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (int64_t)(ms % 1000) * 1000000LL;
    if (ts->tv_nsec >= 1000000000LL) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000LL;
    }
}
