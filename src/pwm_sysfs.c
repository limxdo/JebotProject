#include "../include/pwm_sysfs.h"
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>

#define WAIT_US 40000
#define TIMER   25

int pwm_open(pwm_t *pwm, unsigned int chip, unsigned int channel) {

    if (!pwm) {
        errno = EINVAL;
        return -1;
    }

    int err = 0;

    pwm->chip = chip;
    pwm->channel = channel;

    /* default values */
    pwm->period_ns = 0;
    pwm->duty_cycle_ns = 0;
    pwm->enable = false;

    char path[PATH_SIZE];

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip%d", pwm->chip);
    snprintf(pwm->export_path, sizeof(pwm->export_path), "%s/export", path);
    snprintf(pwm->unexport_path, sizeof(pwm->unexport_path), "%s/unexport", path);
    snprintf(pwm->period_path, sizeof(pwm->period_path), "%s/pwm%d/period", path, pwm->channel);
    snprintf(pwm->duty_path, sizeof(pwm->duty_path), "%s/pwm%d/duty_cycle", path, pwm->channel);
    snprintf(pwm->enable_path, sizeof(pwm->enable_path), "%s/pwm%d/enable", path, pwm->channel);

    pwm->export_file = fopen(pwm->export_path, "w");
    pwm->unexport_file = fopen(pwm->unexport_path, "w");

    if (!pwm->export_file || !pwm->unexport_file) {
        err = errno;
        goto error;
    }

    /* write channel to export */
    fprintf(pwm->export_file, "%d", pwm->channel);
    fflush(pwm->export_file);
    err = errno;

    /* wait kernel to make sysfs */
    int timer = TIMER;
    while (access(pwm->period_path, F_OK) != 0) { // if file not created
        usleep(WAIT_US);
        if (timer-- == 0) // if waiting 1 second and kernel not create file
            goto error;
    }
    timer = TIMER;
    while (access(pwm->duty_path, F_OK) != 0) {
        usleep(WAIT_US);
        if (timer-- == 0)
            goto error;
    }
    timer = TIMER;
    while (access(pwm->enable_path, F_OK) != 0) {
        usleep(WAIT_US);
        if (timer-- == 0)
            goto error;
    }
    err = 0;

    /* double check */
    usleep(15000);
    
    /* open FILEs */
    pwm->period_file = fopen(pwm->period_path, "w");
    pwm->duty_file = fopen(pwm->duty_path, "w");
    pwm->enable_file = fopen(pwm->enable_path, "w");

    if (!pwm->period_file || !pwm->duty_file || !pwm->enable_file)
        goto error;

    return 0;

error:
    pwm_close(pwm);

    if (err) errno = err;
    return -1;
}

int pwm_set_period(pwm_t *pwm, uint64_t period_ns) {

    /* if NULL */
    if (!pwm || !pwm->period_file) {
        errno = EINVAL;
        return -1;
    }

    /* kernel error */
    if (pwm->duty_cycle_ns > period_ns) {
        errno = EINVAL;
        return -1;
    }

    pwm->period_ns = period_ns;

    /* write period to sysfs */
    fprintf(pwm->period_file, "%" PRIu64, pwm->period_ns);
    if (fflush(pwm->period_file) < 0)
        return -1;

    return 0;
}

int pwm_set_period_hz(pwm_t *pwm, uint32_t hz) {
    if (hz == 0) {
        errno = EINVAL;
        return -1;
    }
    uint64_t period_ns = 1000000000 / hz;
    return pwm_set_period(pwm, period_ns);
}

int pwm_set_duty_cycle(pwm_t *pwm, uint64_t duty_cycle) {

    /* if NULL */
    if (!pwm || !pwm->duty_file) {
        errno = EINVAL;
        return -1;
    }

    /* kernel error */
    if (duty_cycle > pwm->period_ns) {
        errno = EINVAL;
        return -1;
    }

    pwm->duty_cycle_ns = duty_cycle;

    fprintf(pwm->duty_file, "%" PRIu64, pwm->duty_cycle_ns);
    if (fflush(pwm->duty_file) < 0)
        return -1;

    return 0;
}

int pwm_set_duty_cycle_percentage(pwm_t *pwm, uint8_t percent) {
    if (percent > 100) {
        errno = EINVAL;
        return -1;
    }
    uint64_t duty_cycle = (pwm->period_ns * percent) / 100;
    return pwm_set_duty_cycle(pwm, duty_cycle);
}

int pwm_enable(pwm_t *pwm, bool enable) {

    /* if NULL */
    if (!pwm || !pwm->enable_file) {
        errno = EINVAL;
        return -1;
    }

    pwm->enable = enable;

    fprintf(pwm->enable_file, "%d", pwm->enable);
    if (fflush(pwm->enable_file) < 0)
        return -1;

    return 0;
}

int pwm_close(pwm_t *pwm) {

    /* if NULL */
    if (!pwm) {
        errno = EINVAL;
        return -1;
    }

    pwm_enable(pwm, false);
    pwm_set_duty_cycle(pwm ,0);
    pwm_set_period(pwm, 0);

    /* close FILEs */
    if (pwm->enable_file) {
        fclose(pwm->enable_file);
        pwm->enable_file = NULL;
    }

    if (pwm->duty_file) {
        fclose(pwm->duty_file);
        pwm->duty_file = NULL;
    }

    if (pwm->period_file) {
        fclose(pwm->period_file);
        pwm->period_file = NULL;
    }
    
    if (pwm->export_file) {
        fclose(pwm->export_file);
        pwm->export_file = NULL;
    }

    /* unexport & close */
    if (pwm->unexport_file) {
        fprintf(pwm->unexport_file, "%d", pwm->channel);
        if (fflush(pwm->unexport_file) < 0)
            return -1;
        fclose(pwm->unexport_file);
        pwm->unexport_file = NULL;
    }

    return 0;
}
