#ifndef GPIO_CHARDEV_H
#define GPIO_CHARDEV_H

#include <linux/gpio.h>
#include <stdbool.h>

typedef struct {
    int chip_fd;
    char consumer_label[GPIO_MAX_NAME_SIZE];

    struct {
        int line_fd;
        int level;
        bool claimed;
        unsigned long flags;
    } lines[GPIOHANDLES_MAX];
} gpio_chip_t;

int gpio_chip_open(gpio_chip_t *chip, int chip_num, char *consumer_label);
void gpio_chip_close(gpio_chip_t *chip);
int gpio_claim_output(gpio_chip_t *chip, int gpio, int default_value);
int gpio_claim_input(gpio_chip_t *chip, int gpio);
void gpio_free(gpio_chip_t *chip, int gpio);
int gpio_write(gpio_chip_t *chip, int gpio, int level);
int gpio_read(gpio_chip_t *chip, int gpio);

#endif
