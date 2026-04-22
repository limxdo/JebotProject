#ifndef PWM_SYSFS_H
#define PWM_SYSFS_H

#define PWM_INIT {0}
#define PWM_PERIOD_MAX 20000000

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define PATH_SIZE 256

typedef struct {
    unsigned int chip;
    unsigned int channel;

    char export_path[PATH_SIZE];
    char unexport_path[PATH_SIZE];
    char period_path[PATH_SIZE];
    char duty_path[PATH_SIZE];
    char enable_path[PATH_SIZE];

    FILE *export_file;
    FILE *unexport_file;
    FILE *period_file;
    FILE *duty_file;
    FILE *enable_file;

    uint64_t period;
    uint64_t duty_cycle;
    bool enable;
} pwm_t;

int pwm_open(pwm_t *pwm, unsigned int chip, unsigned int channel);
int pwm_set_period(pwm_t *pwm, uint64_t period);
int pwm_set_duty_cycle(pwm_t *pwm, uint64_t duty);
int pwm_enable(pwm_t *pwm, bool enable);
int pwm_close(pwm_t *pwm);

#endif
