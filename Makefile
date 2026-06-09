# compiler
CFLAGS 	:= -Wall -Wextra -Wno-format-truncation -Wno-unused-parameter
LDFLAGS := -llgpio
CC 		:= gcc

# paths
SRC   := src
BIN   := bin
BUILD := build

# motord
MOTORD_SRC := $(SRC)/motord.c
MOTORD_OBJ := $(BUILD)/motord.o
MOTORD_BIN := $(BIN)/motord
# ultrasonicd
ULTRASONICD_SRC := $(SRC)/ultrasonicd.c
ULTRASONICD_OBJ := $(BUILD)/ultrasonicd.o
ULTRASONICD_BIN := $(BIN)/ultrasonicd

# powerd
POWERD_SRC := $(SRC)/powerd.c
POWERD_OBJ := $(BUILD)/powerd.o
POWERD_BIN := $(BIN)/powerd

# helpers
RUNTIME_SRC := $(SRC)/runtime.c
RUNTIME_OBJ := $(BUILD)/runtime.o

TIMER_SRC := $(SRC)/timer.c
TIMER_OBJ := $(BUILD)/timer.o

PWM_SYSFS_SRC := $(SRC)/pwm_sysfs.c
PWM_SYSFS_OBJ := $(BUILD)/pwm_sysfs.o

# create $(BIN) and $(BUILD) first
$(shell mkdir -p $(BUILD) $(BIN))

all: $(MOTORD_BIN) $(POWERD_BIN) $(ULTRASONICD_BIN)

$(MOTORD_BIN): $(MOTORD_OBJ) $(RUNTIME_OBJ) $(TIMER_OBJ) $(PWM_SYSFS_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) 

$(MOTORD_OBJ): $(MOTORD_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<


$(POWERD_BIN): $(POWERD_OBJ) $(RUNTIME_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) 

$(POWERD_OBJ): $(POWERD_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<


$(ULTRASONICD_BIN): $(ULTRASONICD_OBJ) $(TIMER_OBJ) $(RUNTIME_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lpthread

$(ULTRASONICD_OBJ): $(ULTRASONICD_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<


$(RUNTIME_OBJ): $(RUNTIME_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TIMER_OBJ): $(TIMER_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<

$(PWM_SYSFS_OBJ): $(PWM_SYSFS_SRC)
	$(CC) $(CFLAGS) -c -o $@ $<


clean:
	rm -rf $(BUILD) $(BIN)

# install
install:
	@./installer.sh install

# uninstall
uninstall:
	@./installer.sh uninstall


.PHONY: all clean install uninstall
