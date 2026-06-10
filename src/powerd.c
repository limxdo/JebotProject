#include "../include/runtime.h"
#include "../include/logger.h"

#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <lgpio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

int gpio = -1;
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

void ina219_set_config(int fd, uint16_t config) {
    uint8_t buf[3] = {
        INA219_CONFIG_REG,
        (config >> 8) & 0xFF,
        config & 0xFF
    };

    write(fd, buf, 3);
}

void ina219_set_calibration(int fd, uint16_t cal) { 
    uint8_t buf[3] = {
        INA219_CAL_REG,
        (cal >> 8) & 0xFF,
        cal & 0xFF
    };

    write(fd, buf, 3);
}

float ina219_get_vshunt(int fd) {
    /*
     * 1 bit = 10 uV
     *
     * example:
     *      Vshunt_mV = raw * 0.01
     *      Vshunt_V  = raw * 0.00001
     */

    uint8_t reg = INA219_VSHUNT_REG;
    uint8_t buf[2];

    write(fd, &reg, 1);
    read(fd, buf, 2);

    int16_t vshunt_raw = (int16_t)((buf[0] << 8) | buf[1]);
    return vshunt_raw * 0.01; // mV
}

float ina219_get_vbus(int fd) {
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

    write(fd, &reg, 1);
    read(fd, buf, 2);

    uint16_t vbus_raw = (uint16_t)((buf[0] << 8) | buf[1]);
    vbus_raw = vbus_raw >> 3;
    return vbus_raw * 0.004; // V
}

float ina219_get_current(int fd, float lsb) {
    /*
     * current_A = raw * Current_LSB
     */

    uint8_t reg = INA219_CURRENT_REG;
    uint8_t buf[2];

    write(fd, &reg, 1);
    read(fd, buf, 2);

    int16_t current_raw = (int16_t)((buf[0] << 8) | buf[1]);
    return current_raw * lsb;
}

float ina219_get_power(int fd, float lsb) {
    /*
     * power_W = raw * Power_LSB
     */

    uint8_t reg = INA219_POWER_REG;
    uint8_t buf[2];

    write(fd, &reg, 1);
    read(fd, buf, 2);

    int16_t power_raw = (int16_t)((buf[0] << 8) | buf[1]);
    return power_raw * lsb;
}

void ina219_get_all(struct ina219 *ina) {
    ina->vshunt_mV = ina219_get_vshunt(ina->i2c_fd);
    ina->vbus_V    = ina219_get_vbus(ina->i2c_fd);
    ina->current_A = ina219_get_current(ina->i2c_fd, ina->current_lsb);
    ina->power_W   = ina219_get_power(ina->i2c_fd, ina->power_lsb);
}

void ina219_write_values(struct ina219 *ina) {
    /* Vshunt */
    runtime_write_atomic(INA219_VSHUNT_PATH, 0644, "%.3f\n", ina->vshunt_mV);

    /* Vbus */
    runtime_write_atomic(INA219_VBUS_PATH, 0644, "%.3f\n", ina->vbus_V);

    /* Current */
    runtime_write_atomic(INA219_CURRENT_PATH, 0644, "%.4f\n", ina->current_A);

    /* Power */
    runtime_write_atomic(INA219_POWER_PATH, 0644, "%.4f\n", ina->power_W);
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
    bool charging;
};

float x1201_get_capacity(int fd) {
    /*
     * capacity = capacity_raw / 256.0
     */

    uint8_t reg = X1201_CAPACITY_REG;
    uint8_t buf[2];
    uint16_t capacity_raw;

    write(fd, &reg, 1);
    read(fd, &buf, 2);

    capacity_raw = (buf[0] << 8) | buf[1];
    
    float capacity = capacity_raw / 256.0;

    return capacity > 100 ? 100 : capacity;
}

float x1201_get_voltage(int fd) {
    /*
     * voltage_V = voltage_raw * 1.25 / 1000.0 / 16.0
     */

    uint8_t reg = X1201_VOLTAGE_REG;
    uint8_t buf[2];
    uint16_t voltage_raw;

    write(fd, &reg, 1);
    read(fd, &buf, 2);

    voltage_raw = (buf[0] << 8) | buf[1];

    float voltage_V = voltage_raw * 1.25 / 1000.0 / 16.0;

    return voltage_V;
}

void x1201_get_all(struct x1201 *hat) {
    hat->voltage_V = x1201_get_voltage(hat->i2c_fd);
    hat->capacity = x1201_get_capacity(hat->i2c_fd);
    hat->charging = lgGpioRead(gpio, X1201_POWER_LOSS_GPIO);
}

void x1201_write_values(struct x1201 *hat) {
    /* Voltage */
    runtime_write_atomic(X1201_VOLTAGE_PATH, 0644, "%.3f\n", hat->voltage_V);

    /* Capacity */
    runtime_write_atomic(X1201_CAPACITY_PATH, 0644, "%.2f\n", hat->capacity);

    /* Charging */
    runtime_write_atomic(X1201_CHARGING_PATH, 0644, "%d\n", hat->charging);
}


void handler(int signum) {
    running = false;
}


int main(void) {

    int exit_status = 0;

    signal(SIGINT, handler);
    signal(SIGTERM, handler);

    
    /* runtime setup */
    if (runtime_init("powerd", 0755) < 0) {
        log_fatal("Runtime failed: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }
    runtime_pid(getpid());

    /* gpio */
    gpio = lgGpiochipOpen(0);
    if (gpio < 0) {
        log_fatal("lgGpiochipOpen: %s\n", lguErrorText(gpio));
        exit_status = 1;
        goto exit;
    }

    lgGpioClaimInput(gpio, LG_SET_PULL_NONE, X1201_POWER_LOSS_GPIO);
    lgGpioClaimOutput(gpio, LG_SET_OUTPUT, X1201_CHARGING_CTRL_GPIO, 0); // enable charging

    /* INA219 */ 
    struct ina219 ina219 = {0};

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
    struct x1201 x1201 = {0};

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
        ina219_get_all(&ina219);
        ina219_write_values(&ina219);

        x1201_get_all(&x1201);
        x1201_write_values(&x1201);

        usleep(1);
    }

exit:
    close(ina219.i2c_fd);
    close(x1201.i2c_fd);

    lgGpiochipClose(gpio);

    runtime_exit();

    return exit_status;
}
