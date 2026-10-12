#include "../include/runtime.h"
#include "../include/logger.h"
#include "../include/gpio_chardev.h"

#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

gpio_chip_t chip0;
volatile bool running = true;


#define I2C_BUS "/dev/i2c-1"
#define RSHUNT 0.0015 // (75mV / 50A) Ohms

/************** INA219 **************/
#define INA219_RUNTIME "ina219"
#define INA219_VSHUNT_PATH   INA219_RUNTIME "/shunt_voltage"
#define INA219_VBUS_PATH     INA219_RUNTIME "/bus_voltage"
#define INA219_POWER_PATH    INA219_RUNTIME "/power"
#define INA219_CURRENT_PATH  INA219_RUNTIME "/current"


#define INA219_ADDR        0x40

#define INA219_CONFIG_REG  0x00 // R/W
#define INA219_VSHUNT_REG  0x01 // R
#define INA219_VBUS_REG    0x02 // R
#define INA219_POWER_REG   0x03 // R
#define INA219_CURRENT_REG 0x04 // R
#define INA219_CAL_REG     0x05 // R/W

/* config */
#define RST      0b0     // reset off
#define EMP      0b0     // empty bit (0)
#define BRNG     0b0     // 16V
#define PG       0b01    // +/-80mV
#define BADC     0b1011  // 12-bit, 8 samples, 4.26ms
#define SADC     0b1011  // ^^^^^^^^^^^^^^^^^^^^^^^^^
#define MODE     0b111   // Shunt and bus, continuous
// Bits
#define RST_BIT  15
#define EMP_BIT  14
#define BRNG_BIT 13
#define PG_BIT   11
#define BADC_BIT 7
#define SADC_BIT 3
#define MODE_BIT 0

/* Assembling the config bits */
#define INA219_CONFIG (  \
    (RST  << RST_BIT)  | \
    (EMP  << EMP_BIT)  | \
    (BRNG << BRNG_BIT) | \
    (PG   << PG_BIT)   | \
    (BADC << BADC_BIT) | \
    (SADC << SADC_BIT) | \
    (MODE << MODE_BIT)   \
)

/* Calibrations */
#define INA219_CAL_FACTOR 0.04096

/* Current_LSB = Maximum_Expected_Current / 32767 */
/* Max_Expected_Current = ~55A (measured during hard inrush test) */
#define INA219_CURRENT_LSB  (55.0 / 32767.0)

/* Power_LSB = 20 * Current_LSB */
#define INA219_POWER_LSB    (20 * INA219_CURRENT_LSB)

#define INA219_CALIBRATION (INA219_CAL_FACTOR / (INA219_CURRENT_LSB * RSHUNT))

struct ina219 {
    int i2c_fd;

    uint16_t config;
    uint16_t calibration;

    float current_lsb;
    float power_lsb;

    float vshunt_mV;
    float vbus_V;
    float current_A;
    float power_W;
};

int ina219_set_config(int fd, uint16_t config) {
    uint8_t buf[3] = {
        INA219_CONFIG_REG,
        (config >> 8) & 0xFF,
        config & 0xFF
    };

    /* i2c check */
    if (write(fd, buf, 3) < 0)
        return -1;

    return 0;
}

int ina219_set_calibration(int fd, uint16_t cal) { 
    uint8_t buf[3] = {
        INA219_CAL_REG,
        (cal >> 8) & 0xFF,
        cal & 0xFF
    };

    /* i2c check */
    if (write(fd, buf, 3) < 0)
        return -1;

    return 0;
}

int ina219_get_vshunt(int fd, float *out) {
    /*
     * 1 bit = 10 uV
     *
     * example:
     *      Vshunt_mV = raw * 0.01
     *      Vshunt_V  = raw * 0.00001
     */

    uint8_t reg = INA219_VSHUNT_REG;
    uint8_t buf[2];

    /* i2c check */
    if (
        write(fd, &reg, 1) < 0 ||
        read(fd, buf, 2) < 0
    ) {
        return -1;
    }


    int16_t vshunt_raw = (int16_t)((buf[0] << 8) | buf[1]);
    *out = vshunt_raw * 0.01; // mV

    return 0;
}

int ina219_get_vbus(int fd, float *out) {
    /*
     * 1 bit = 4 mV
     *
     * last 3 bits is not a voltage: (raw >> 3)
     *
     * example:
     *      Vbus_V = (raw >> 3) * 0.004
     */

    uint8_t reg = INA219_VBUS_REG;
    uint8_t buf[2];

    if (
        write(fd, &reg, 1) < 0 ||
        read(fd, buf, 2) < 0
    ) {
        return -1;
    }


    uint16_t vbus_raw = (uint16_t)((buf[0] << 8) | buf[1]);
    vbus_raw = vbus_raw >> 3;
    *out = vbus_raw * 0.004; // V

    return 0;
}

int ina219_get_current(int fd, float lsb, float *out) {
    /*
     * current_A = raw * Current_LSB
     */

    uint8_t reg = INA219_CURRENT_REG;
    uint8_t buf[2];

    if (
        write(fd, &reg, 1) < 0 ||
        read(fd, buf, 2) < 0
    ) {
        return -1;
    }


    int16_t current_raw = (int16_t)((buf[0] << 8) | buf[1]);
    *out = current_raw * lsb; // A

    return 0;
}

int ina219_get_power(int fd, float lsb, float *out) {
    /*
     * power_W = raw * Power_LSB
     */

    uint8_t reg = INA219_POWER_REG;
    uint8_t buf[2];

    if (
        write(fd, &reg, 1) < 0 ||
        read(fd, buf, 2) < 0
    ) {
        return -1;
    }

    int16_t power_raw = (int16_t)((buf[0] << 8) | buf[1]);
    *out = power_raw * lsb; // W

    return 0;
}


/********** X1201 (PowerHat) **********/
#define X1201_RUNTIME "x1201"
#define X1201_VOLTAGE_PATH  X1201_RUNTIME "/voltage"
#define X1201_CAPACITY_PATH X1201_RUNTIME "/capacity"
#define X1201_CHARGING_PATH X1201_RUNTIME "/charging"

#define X1201_ADDR         0x36

#define X1201_VOLTAGE_REG  0x02
#define X1201_CAPACITY_REG 0x04

#define X1201_POWER_LOSS_GPIO    6  // if read HIGH: charger plugged, if read LOW: charger unplugged
#define X1201_CHARGING_CTRL_GPIO 16 // if write HIGH: disable charging, if write LOW: enable charging

struct x1201 {
    int i2c_fd;

    float voltage_V;
    float capacity;
    int8_t charging;
};

int x1201_get_capacity(int fd, float *out) {
    /*
     * capacity = capacity_raw / 256.0
     */

    uint8_t reg = X1201_CAPACITY_REG;
    uint8_t buf[2];
    uint16_t capacity_raw;

    if (
        write(fd, &reg, 1) < 0 ||
        read(fd, buf, 2) < 0
    ) {
        return -1;
    }

    capacity_raw = (buf[0] << 8) | buf[1];
    
    float capacity = capacity_raw / 256.0;

    *out = capacity > 100 ? 100 : capacity;

    return 0;
}

int x1201_get_voltage(int fd, float *out) {
    /*
     * voltage_V = voltage_raw * 1.25 / 1000.0 / 16.0
     */

    uint8_t reg = X1201_VOLTAGE_REG;
    uint8_t buf[2];
    uint16_t voltage_raw;

    if (
        write(fd, &reg, 1) < 0 ||
        read(fd, buf, 2) < 0
    ) {
        return -1;
    }

    voltage_raw = (buf[0] << 8) | buf[1];

    float voltage_V = voltage_raw * 1.25 / 1000.0 / 16.0;

    *out = voltage_V;

    return 0;
}


void handler(int signum) {
    running = false;
}


int main(void) {

    signal(SIGINT, handler);
    signal(SIGTERM, handler);

    /* init vars */
    int exit_status = 0;

    struct ina219 ina219 = {
        .i2c_fd = -1
    };

    struct x1201 x1201 = {
        .i2c_fd = -1
    };

    int runtime_init_ret = -1;
    int gpio_chip_open_ret = -1;
    
    /* runtime setup */
    if ((runtime_init_ret = runtime_init("powerd", 0755)) < 0) {
        log_fatal("Runtime failed: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }
    runtime_pid(getpid());

    /* gpio */
    if ((gpio_chip_open_ret = gpio_chip_open(&chip0, 0, "powerd")) < 0) {
        log_fatal("gpio_chip_open: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }

    if (gpio_claim_input(&chip0, X1201_POWER_LOSS_GPIO, 0) < 0) {
        log_fatal("gpio_claim_input: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }

    if (gpio_claim_output(&chip0, X1201_CHARGING_CTRL_GPIO, 0, 0/* enable charging by default */) < 0) {
        log_fatal("gpio_claim_output: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }

    /* INA219 */ 
    ina219.i2c_fd = open(I2C_BUS, O_RDWR);
    if (ina219.i2c_fd < 0) {
        log_fatal("open %s (%s): %s\n", I2C_BUS, "INA219", strerror(errno));
        exit_status = 1;
        goto exit;
    }

    if (ioctl(ina219.i2c_fd, I2C_SLAVE, INA219_ADDR) < 0) {
        log_fatal("ioctl 0x%x: %s\n", INA219_ADDR, strerror(errno));
        exit_status = 1;
        goto exit;
    }

    ina219.config      = INA219_CONFIG;
    ina219.calibration = INA219_CALIBRATION;
    ina219.current_lsb = INA219_CURRENT_LSB;
    ina219.power_lsb   = INA219_POWER_LSB;

    ina219_set_config(ina219.i2c_fd, ina219.config);
    usleep(10000);
    ina219_set_calibration(ina219.i2c_fd, ina219.calibration);
    usleep(10000);
    
    /* INA219 runtime */
    runtime_mkdir(INA219_RUNTIME, 0755);


    /* X1201 */
    x1201.i2c_fd = open(I2C_BUS, O_RDWR);

    if (x1201.i2c_fd < 0) {
        log_fatal("open %s (%s): %s\n", I2C_BUS, "X1201", strerror(errno));
        exit_status = 1;
        goto exit;
    }

    if (ioctl(x1201.i2c_fd, I2C_SLAVE, X1201_ADDR) < 0) {
        log_fatal("ioctl 0x%x: %s\n", X1201_ADDR, strerror(errno));
        exit_status = 1;
        goto exit;
    }

    /* X1201 runtime */
    runtime_mkdir(X1201_RUNTIME, 0755);


    while (running) {
        /* INA219 */
        if (ina219_get_vshunt(ina219.i2c_fd, &ina219.vshunt_mV) < 0) {
            log_err("i2c failed: %s\n", strerror(errno));
            runtime_write_atomic(INA219_VSHUNT_PATH, 0644, "error\n");
        } else {
            runtime_write_atomic(INA219_VSHUNT_PATH, 0644, "%.3f\n", ina219.vshunt_mV);
        }

        if (ina219_get_vbus(ina219.i2c_fd, &ina219.vbus_V) < 0) {
            log_err("i2c failed: %s\n", strerror(errno));
            runtime_write_atomic(INA219_VBUS_PATH, 0644, "error\n");
        } else {
            runtime_write_atomic(INA219_VBUS_PATH, 0644, "%.3f\n", ina219.vbus_V);
        }

        if (ina219_get_current(ina219.i2c_fd, ina219.current_lsb, &ina219.current_A) < 0) {
            log_err("i2c failed: %s\n", strerror(errno));
            runtime_write_atomic(INA219_CURRENT_PATH, 0644, "error\n");
        } else {
            runtime_write_atomic(INA219_CURRENT_PATH, 0644, "%.4f\n", ina219.current_A);
        }

        if (ina219_get_power(ina219.i2c_fd, ina219.power_lsb, &ina219.power_W) < 0) {
            log_err("i2c failed: %s\n", strerror(errno));
            runtime_write_atomic(INA219_POWER_PATH, 0644, "error\n");
        } else {
            runtime_write_atomic(INA219_POWER_PATH, 0644, "%.4f\n", ina219.power_W);
        }


        /* X1201 */
        if (x1201_get_voltage(x1201.i2c_fd, &x1201.voltage_V) < 0) {
            log_err("i2c failed: %s\n", strerror(errno));
            runtime_write_atomic(X1201_VOLTAGE_PATH, 0644, "error\n");
        } else {
            runtime_write_atomic(X1201_VOLTAGE_PATH, 0644, "%.3f\n", x1201.voltage_V);
        }

        if (x1201_get_capacity(x1201.i2c_fd, &x1201.capacity) < 0) {
            log_err("i2c failed: %s\n", strerror(errno));
            runtime_write_atomic(X1201_CAPACITY_PATH, 0644, "error\n");
        } else {
            runtime_write_atomic(X1201_CAPACITY_PATH, 0644, "%.2f\n", x1201.capacity);
        }

        if ((x1201.charging = gpio_read(&chip0, X1201_POWER_LOSS_GPIO)) < 0) {
            log_err("gpio failed: %s\n", strerror(errno));
            runtime_write_atomic(X1201_CHARGING_PATH, 0644, "error\n");
        } else {
            runtime_write_atomic(X1201_CHARGING_PATH, 0644, "%d\n", x1201.charging);
        }

        usleep(5000);
    }

exit:
    if (ina219.i2c_fd >= 0)
        close(ina219.i2c_fd);

    if (x1201.i2c_fd >= 0)
        close(x1201.i2c_fd);

    if (gpio_chip_open_ret >= 0)
        gpio_chip_close(&chip0);

    if (runtime_init_ret >= 0)
        runtime_exit();

    return exit_status;
}
