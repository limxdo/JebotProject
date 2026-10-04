#include "../include/gpio_chardev.h"

#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

int gpio_chip_open(gpio_chip_t *chip, int chip_num, char *consumer_label) {
    if (!chip || chip_num < 0) return -1;

    memset(chip->lines, 0, sizeof(chip->lines));

    char chardev_path[30];

    snprintf(chardev_path, sizeof(chardev_path), "/dev/gpiochip%d", chip_num);

    if ((chip->chip_fd = open(chardev_path, O_RDWR)) < 0)
        return -1;

    if (!consumer_label) *chip->consumer_label = 0;
    else strncpy(chip->consumer_label, consumer_label, sizeof(chip->consumer_label));

    return 0;
}

void gpio_chip_close(gpio_chip_t *chip) {
    for (size_t i = 0; i < GPIOHANDLES_MAX; i++) {
        if (chip->lines[i].claimed) {
            gpio_free(chip, i);
        }
    }

    close(chip->chip_fd);
}

int gpio_claim_output(gpio_chip_t *chip, int gpio, int default_value) {

    struct gpiohandle_request req = {
        .lines = 1,
        .flags = GPIOHANDLE_REQUEST_OUTPUT,
        .lineoffsets[0] = gpio,
        .default_values[0] = default_value,
        0
    };

    if (*chip->consumer_label)
        strncpy(req.consumer_label, chip->consumer_label, sizeof(req.consumer_label));

    if (ioctl(chip->chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        return -1;
    }

    chip->lines[gpio].line_fd = req.fd;
    chip->lines[gpio].flags = GPIOHANDLE_REQUEST_OUTPUT;
    chip->lines[gpio].level = default_value;
    chip->lines[gpio].claimed = true;

    return 0;
}

int gpio_claim_input(gpio_chip_t *chip, int gpio) {

    struct gpiohandle_request req = {
        .lines = 1,
        .flags = GPIOHANDLE_REQUEST_INPUT,
        .lineoffsets[0] = gpio,
        0
    };

    if (*chip->consumer_label)
        strncpy(req.consumer_label, chip->consumer_label, sizeof(req.consumer_label));

    if (ioctl(chip->chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        return -1;
    }

    chip->lines[gpio].line_fd = req.fd;
    chip->lines[gpio].flags = GPIOHANDLE_REQUEST_INPUT;
    chip->lines[gpio].claimed = true;

    return 0;
}

void gpio_free(gpio_chip_t *chip, int gpio) {
    if (!chip->lines[gpio].claimed) return;

    close(chip->lines[gpio].line_fd);
    chip->lines[gpio].claimed = false;
    chip->lines[gpio].line_fd = -1;
}

int gpio_write(gpio_chip_t *chip, int gpio, int level) {
    if (!chip->lines[gpio].claimed || !(chip->lines[gpio].flags & GPIOHANDLE_REQUEST_OUTPUT))
        return -1;

    struct gpiohandle_data data;
    data.values[0] = level;

    if (ioctl(chip->lines[gpio].line_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) < 0) {
        return -1;
    }

    chip->lines[gpio].level = level;

    return 0;
}

int gpio_read(gpio_chip_t *chip, int gpio) {
    if (!chip->lines[gpio].claimed || !(chip->lines[gpio].flags & GPIOHANDLE_REQUEST_INPUT))
        return -1;

    struct gpiohandle_data data;

    if (ioctl(chip->lines[gpio].line_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
        return -1;
    }

    chip->lines[gpio].level = data.values[0];

    return chip->lines[gpio].level;
}
