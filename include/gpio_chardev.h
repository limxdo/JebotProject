#ifndef GPIO_CHARDEV_H
#define GPIO_CHARDEV_H

#include <linux/gpio.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include <stdint.h>

/* callback function for event */
typedef void (*gpio_event_callback_t)(int, int, void*);

/* gpio chip declaration*/
typedef struct gpio_chip gpio_chip_t;

/* data uses for event thread function */
struct gpio_event_thread_data {
    int gpio;
    gpio_chip_t *chip;
    gpio_event_callback_t callback;
    void *userdata;
};

/* data uses for software pwm thread function */
struct gpio_pwm_thread_data {
    int gpio;
    gpio_chip_t *chip;
    struct timespec on_ts, off_ts;
};

/* gpio_chip defination */
struct gpio_chip {
    int chip_fd;
    char consumer_label[GPIO_MAX_NAME_SIZE];

    struct {
        bool claimed;
        __u32 flags;

        /* normal I/O */
        int line_fd;
        int level;

        /* events */
        int event_fd;
        struct gpio_event_thread_data event_data;
        pthread_t event_thread_id;

        /* software pwm */
        bool pwm_running;
        struct gpio_pwm_thread_data pwm_data;
        pthread_t pwm_thread_id;
        
    } lines[GPIOHANDLES_MAX];
};

int gpio_chip_open(gpio_chip_t *chip, int chip_num, char *consumer_label);
void gpio_chip_close(gpio_chip_t *chip);
int gpio_claim_output(gpio_chip_t *chip, int gpio, __u32 handleflags, int default_value);
int gpio_claim_input(gpio_chip_t *chip, int gpio, __u32 handleflags);
void gpio_free(gpio_chip_t *chip, int gpio);
int gpio_write(gpio_chip_t *chip, int gpio, int level);
int gpio_read(gpio_chip_t *chip, int gpio);
int gpio_claim_event(gpio_chip_t *chip, int gpio, __u32 eventflags, __u32 handleflags, gpio_event_callback_t callback, void *userdata);
int gpio_software_pwm(gpio_chip_t *chip, int gpio, uint32_t freq_hz, float duty_cycle_percent);

#endif
