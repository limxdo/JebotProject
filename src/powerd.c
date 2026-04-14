#include "../include/runtime.h"
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <lgpio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

int gpio = -1;
volatile bool running = true;


#define I2C_BUS "/dev/i2c-1"
#define RSHUNT 0.0015 // (75mV / 50A) Ohms

/* runtime path */
#define POWERD_RUNTIME_PATH "powerd"

/************** INA219 **************/
#define INA219_RUNTIME_PATH "ina219"
#define INA219_VSHUNT_FILE   INA219_RUNTIME_PATH "/shunt_voltage"
#define INA219_VBUS_FILE     INA219_RUNTIME_PATH "/bus_voltage"
#define INA219_POWER_FILE    INA219_RUNTIME_PATH "/power"
#define INA219_CURRENT_FILE  INA219_RUNTIME_PATH "/current"


#define INA219_ADDR        0x40

#define INA219_CONFIG_REG  0x00 // R/W
#define INA219_VSHUNT_REG  0x01 // R
#define INA219_VBUS_REG    0x02 // R
#define INA219_POWER_REG   0x03 // R
#define INA219_CURRENT_REG 0x04 // R
#define INA219_CAL_REG     0x05 // R/W

/* config */
#define RST         0b0     // reset off
#define EMP         0b0     // empty bit (0)
#define BRNG        0b0     // 16V
#define PG          0b01    // +/-80mV
#define BADC        0b1011  // 12-bit, 8 samples, 4.26ms
#define SADC        0b1011  // ^^^^^^^^^^^^^^^^^^^^^^^^^
#define MODE        0b111   // Shunt and bus, continuous
// Bits
#define RST_BIT     15
#define EMP_BIT     14
#define BRNG_BIT    13
#define PG_BIT      11
#define BADC_BIT    7
#define SADC_BIT    3
#define MODE_BIT    0

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

/* Current_LSB = Maximum_Expected_Current / 32767   */
/* (32767 is 16-bit Max) */
#define INA219_CURRENT_LSB  (35.0 / 32767.0)
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

    int vshunt_fd;
    int vbus_fd;
    int current_fd;
    int power_fd;
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
    ina->vshunt_mV  = ina219_get_vshunt(ina->i2c_fd);
    ina->vbus_V    = ina219_get_vbus(ina->i2c_fd);
    ina->current_A = ina219_get_current(ina->i2c_fd, ina->current_lsb);
    ina->power_W   = ina219_get_power(ina->i2c_fd, ina->power_lsb);
}

void ina219_write_values(struct ina219 *ina) {
    char buf[32];

    /* Vshunt */
    snprintf(buf, sizeof(buf), "%.3f\n", ina->vshunt_mV);
    ftruncate(ina->vshunt_fd, 0);
    lseek(ina->vshunt_fd, 0, SEEK_SET);
    write(ina->vshunt_fd, buf, strlen(buf));

    /* Vbus */
    snprintf(buf, sizeof(buf), "%.3f\n", ina->vbus_V);
    ftruncate(ina->vbus_fd, 0);
    lseek(ina->vbus_fd, 0, SEEK_SET);
    write(ina->vbus_fd, buf, strlen(buf));

    /* Current */
    snprintf(buf, sizeof(buf), "%.4f\n", ina->current_A);
    ftruncate(ina->current_fd, 0);
    lseek(ina->current_fd, 0, SEEK_SET);
    write(ina->current_fd, buf, strlen(buf));

    /* Power */
    snprintf(buf, sizeof(buf), "%.4f\n", ina->power_W);
    ftruncate(ina->power_fd, 0);
    lseek(ina->power_fd, 0, SEEK_SET);
    write(ina->power_fd, buf, strlen(buf));
}


/********** X1201 (PowerHat) **********/
#define X1201_RUNTIME_PATH "x1201"
#define X1201_VOLTAGE_FILE    X1201_RUNTIME_PATH "/voltage"
#define X1201_CAPACITY_FILE   X1201_RUNTIME_PATH "/capacity"
#define X1201_CHARGING_FILE   X1201_RUNTIME_PATH "/charging"

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

    int voltage_fd;
    int capacity_fd;
    int charging_fd;
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

    return capacity;
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
    char buf[32];

    /* Voltage */
    snprintf(buf, sizeof(buf), "%.3f\n", hat->voltage_V);
    ftruncate(hat->voltage_fd, 0);
    lseek(hat->voltage_fd, 0, SEEK_SET);
    write(hat->voltage_fd, buf, strlen(buf));

    /* Capacity */
    snprintf(buf, sizeof(buf), "%.2f\n", hat->capacity);
    ftruncate(hat->capacity_fd, 0);
    lseek(hat->capacity_fd, 0, SEEK_SET);
    write(hat->capacity_fd, buf, strlen(buf));

    /* Charging */
    snprintf(buf, sizeof(buf), "%d\n", hat->charging);
    ftruncate(hat->charging_fd, 0);
    lseek(hat->charging_fd, 0, SEEK_SET);
    write(hat->charging_fd, buf, strlen(buf));
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
        fprintf(stderr, "\033[31mFATAL ERROR:\033[0m Runtime failed: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }
    runtime_pid(getpid());

    /* gpio */
    gpio = lgGpiochipOpen(0);
    if (gpio < 0) {
        fprintf(stderr, "\033[31mFATAL ERROR:\033[0m lgGpiochipOpen: %s\n", lguErrorText(gpio));
        exit_status = 1;
        goto exit;
    }

    lgGpioClaimInput(gpio, LG_SET_PULL_NONE, X1201_POWER_LOSS_GPIO);
    lgGpioClaimOutput(gpio, LG_SET_OUTPUT, X1201_CHARGING_CTRL_GPIO, 0); // enable charging


    /* INA219 */
    struct ina219 ina219 = {0};

    ina219.i2c_fd = open(I2C_BUS, O_RDWR);
    if (ina219.i2c_fd < 0) {
        fprintf(stderr, "FATAL ERROR: open %s (%s): %s\n", I2C_BUS, "INA219", strerror(errno));
        exit_status = 1;
        goto exit;
    }

    if (ioctl(ina219.i2c_fd, I2C_SLAVE, INA219_ADDR) < 0) {
        fprintf(stderr, "FATAL ERROR: ioctl 0x%x: %s\n", INA219_ADDR, strerror(errno));
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
    runtime_mkdir(INA219_RUNTIME_PATH, 0755);
    ina219.vshunt_fd  = runtime_open(INA219_VSHUNT_FILE, O_WRONLY | O_CREAT, 0644);
    ina219.vbus_fd    = runtime_open(INA219_VBUS_FILE, O_WRONLY | O_CREAT, 0644);
    ina219.current_fd = runtime_open(INA219_CURRENT_FILE, O_WRONLY | O_CREAT, 0644);
    ina219.power_fd   = runtime_open(INA219_POWER_FILE, O_WRONLY | O_CREAT, 0644);


    /* X1201 */
    struct x1201 x1201 = {0};

    x1201.i2c_fd = open(I2C_BUS, O_RDWR);

    if (x1201.i2c_fd < 0) {
        fprintf(stderr, "FATAL ERROR: open %s (%s): %s\n", I2C_BUS, "X1201", strerror(errno));
        exit_status = 1;
        goto exit;
    }

    if (ioctl(x1201.i2c_fd, I2C_SLAVE, X1201_ADDR) < 0) {
        fprintf(stderr, "FATAL ERROR: ioctl 0x%x: %s\n", X1201_ADDR, strerror(errno));
        exit_status = 1;
        goto exit;
    }

    /* X1201 runtime */
    runtime_mkdir(X1201_RUNTIME_PATH, 0755);
    x1201.voltage_fd  = runtime_open(X1201_VOLTAGE_FILE, O_WRONLY | O_CREAT, 0644);
    x1201.capacity_fd = runtime_open(X1201_CAPACITY_FILE, O_WRONLY | O_CREAT, 0644);
    x1201.charging_fd = runtime_open(X1201_CHARGING_FILE, O_WRONLY | O_CREAT, 0644);


    printf("PID: %d\n", getpid());
    puts("\n|********** Powerd Is Started **********|");
    while (running) {
        ina219_get_all(&ina219);
        ina219_write_values(&ina219);

        x1201_get_all(&x1201);
        x1201_write_values(&x1201);

        usleep(500000);
    }

exit:
    close(ina219.i2c_fd);
    close(x1201.i2c_fd);

    lgGpiochipClose(gpio);

    runtime_exit();

    return exit_status;
}
