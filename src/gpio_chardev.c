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

    memset(chip, 0, sizeof(gpio_chip_t));

    char chardev_path[30];
    snprintf(chardev_path, sizeof(chardev_path), "/dev/gpiochip%d", chip_num);

    /* open gpiochip */
    if ((chip->chip_fd = open(chardev_path, O_RDWR)) < 0)
        return -1;

    /* if consumer_label */
    if (!consumer_label) *chip->consumer_label = 0;
    else strncpy(chip->consumer_label, consumer_label, sizeof(chip->consumer_label));

    /* default values */
    for (size_t i = 0; i < GPIOHANDLES_MAX; i++) {
        chip->lines[i].line_fd = -1;
        chip->lines[i].event_fd = -1;
    }

    return 0;
}

void gpio_chip_close(gpio_chip_t *chip) {
    if (!chip || chip->chip_fd < 0) return;

    for (size_t i = 0; i < GPIOHANDLES_MAX; i++) {
        /* use `gpio_free` */
        if (chip->lines[i].claimed) {
            gpio_free(chip, i);
        }
    }

    /* close the chip */
    close(chip->chip_fd);
}

int gpio_claim_output(gpio_chip_t *chip, int gpio, int default_value) {
    if (!chip || chip->lines[gpio].claimed) return -1;

    /* handle request for output */
    struct gpiohandle_request req = {0};
    req.lineoffsets[0] = gpio;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = default_value;
    req.lines = 1;

    /* if consumer_label */
    if (*chip->consumer_label)
        strncpy(req.consumer_label, chip->consumer_label, sizeof(req.consumer_label));

    /* request the gpio as OUTPUT */
    if (ioctl(chip->chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0)
        return -1;

    chip->lines[gpio].line_fd = req.fd; // set fd
    chip->lines[gpio].flags = GPIOHANDLE_REQUEST_OUTPUT;
    chip->lines[gpio].level = default_value;
    chip->lines[gpio].claimed = true; // mark it as claimed

    return 0;
}

int gpio_claim_input(gpio_chip_t *chip, int gpio) {
    if (!chip || chip->lines[gpio].claimed) return -1;

    /* handle request for input */
    struct gpiohandle_request req = {0};
    req.lines = 1;
    req.flags = GPIOHANDLE_REQUEST_INPUT;
    req.lineoffsets[0] = gpio;

    if (*chip->consumer_label)
        strncpy(req.consumer_label, chip->consumer_label, sizeof(req.consumer_label));

    /* request the gpio as INPUT */
    if (ioctl(chip->chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0)
        return -1;

    chip->lines[gpio].line_fd = req.fd; // set fd
    chip->lines[gpio].flags = GPIOHANDLE_REQUEST_INPUT;
    chip->lines[gpio].claimed = true; //  mark it as claimed

    return 0;
}

void gpio_free(gpio_chip_t *chip, int gpio) {
    if (!chip || !chip->lines[gpio].claimed) return;

    /* if normal I/O */
    if (chip->lines[gpio].line_fd >= 0) {
        close(chip->lines[gpio].line_fd);
        chip->lines[gpio].line_fd = -1;
        chip->lines[gpio].claimed = false;
    }
    /* if event */
    else if (chip->lines[gpio].event_fd >= 0) {
        pthread_cancel(chip->lines[gpio].event_thread_id);      // close the thread
        pthread_join(chip->lines[gpio].event_thread_id, NULL);  // clean it
        close(chip->lines[gpio].event_fd);
        chip->lines[gpio].event_fd = -1;
        chip->lines[gpio].claimed = false;
    }
}

int gpio_write(gpio_chip_t *chip, int gpio, int level) {
    if (!chip->lines[gpio].claimed || chip->lines[gpio].line_fd < 0 || !(chip->lines[gpio].flags & GPIOHANDLE_REQUEST_OUTPUT))
        return -1;

    /* handle data to request write */
    struct gpiohandle_data data;
    data.values[0] = level;

    /* request to set value */
    if (ioctl(chip->lines[gpio].line_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) < 0)
        return -1;

    /* set gpio current level in chip */
    chip->lines[gpio].level = level;

    return 0;
}

int gpio_read(gpio_chip_t *chip, int gpio) {
    if (!chip->lines[gpio].claimed || chip->lines[gpio].line_fd < 0 || !(chip->lines[gpio].flags & GPIOHANDLE_REQUEST_INPUT))
        return -1;

    /* handle data to request read */
    struct gpiohandle_data data;

    /* request to read value */
    if (ioctl(chip->lines[gpio].line_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0)
        return -1;

    /* set gpio current level in chip */
    chip->lines[gpio].level = data.values[0];

    /* return current value */
    return chip->lines[gpio].level;
}

static void* event_thread_func(void *arg) {
    struct gpio_event_thread_data *event_data = arg; // the arg must be a pointer to `struct gpio_event_thread_data`
    gpio_chip_t *chip = event_data->chip; // get chip
    int gpio = event_data->gpio; // get gpio

    /* event data */
    struct gpioevent_data event;
    ssize_t n;

    while (1) {
        /* read event (blocks until an edge occurs) */
        n = read(chip->lines[gpio].event_fd, &event, sizeof(event));
        if (n > 0) { // if event readed
            chip->lines[gpio].level = (event.id == GPIOEVENT_EVENT_RISING_EDGE) ? 1 : 0; // get current level
            event_data->callback(gpio, chip->lines[gpio].level, event_data->userdata); // call the user callback function
        }
    }

    return NULL;
}

int gpio_claim_event(gpio_chip_t *chip, int gpio, __u32 eventflags, __u32 handleflags, gpio_event_callback_t callback, void *userdata) {
    if (!chip || chip->lines[gpio].claimed)
        return -1;

    /* event request data */
    struct gpioevent_request req = {0};
    req.lineoffset = gpio;
    /* handleflags must include `GPIOHANDLE_REQUEST_INPUT`, as events are input-only */
    req.handleflags = GPIOHANDLE_REQUEST_INPUT | handleflags;
    req.eventflags = eventflags;

    /* if consumer_label */
    if (*chip->consumer_label)
        strncpy(req.consumer_label, chip->consumer_label, sizeof(req.consumer_label));

    /* request the event */
    if (ioctl(chip->chip_fd, GPIO_GET_LINEEVENT_IOCTL, &req) < 0)
        return -1;

    chip->lines[gpio].claimed = true; // mark it as claimed
    chip->lines[gpio].event_fd = req.fd; // set fd
    chip->lines[gpio].flags = GPIOHANDLE_REQUEST_INPUT | handleflags;

    /* set data in `event_data` for the `event_thread_func` */
    chip->lines[gpio].event_data.chip = chip;
    chip->lines[gpio].event_data.gpio = gpio;
    chip->lines[gpio].event_data.callback = callback;
    chip->lines[gpio].event_data.userdata = userdata;

    /* create the thread */
    pthread_create(&chip->lines[gpio].event_thread_id, NULL, event_thread_func, &chip->lines[gpio].event_data);

    return 0;
}
