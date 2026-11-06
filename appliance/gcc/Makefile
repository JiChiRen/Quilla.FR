##############################################################################
# Quilla.FR appliance - minimal FreeRTOS APP behind Quilla QBOOT
#
# This directory is standalone: it includes NO Quilla header or source and
# has no include path outside appliance/. It can be copied away from the
# Quilla checkout and built on its own.
#
# Build (GNU Arm Embedded toolchain on PATH, or pass GCC_PATH=...):
#   make -C appliance/gcc
# Output: appliance/gcc/build/quilla-fr-app.bin
#
# Link origin is 0x08007000 (py32f040xb.ld); do NOT flash this image at 0x0.
# QAPP_BUILD relocates VTOR to 0x7000 in system_py32f040.c.
##############################################################################

TARGET    = quilla-fr-app
BUILD_DIR = build

DEBUG = 1
OPT   = -Os

######################################
# sources
######################################
C_SOURCES = \
../Src/main.c \
../Src/recver.c \
../Src/led.c \
../Src/app_qboot_meta.c \
../Src/app_qboot_secver.c \
../Src/py32f040_hal_msp.c \
../Src/py32f040_it.c \
../Src/system_py32f040.c \
../Drivers/PY32F040_HAL_Driver/Src/py32f040_hal.c \
../Drivers/PY32F040_HAL_Driver/Src/py32f040_hal_cortex.c \
../Drivers/PY32F040_HAL_Driver/Src/py32f040_hal_dma.c \
../Drivers/PY32F040_HAL_Driver/Src/py32f040_hal_gpio.c \
../Drivers/PY32F040_HAL_Driver/Src/py32f040_hal_rcc.c \
../Drivers/PY32F040_HAL_Driver/Src/py32f040_hal_rcc_ex.c \
../Drivers/PY32F040_HAL_Driver/Src/py32f040_hal_uart.c \
../Drivers/PY32F040_HAL_Driver/Src/py32f040_hal_flash.c \
../Drivers/PY32F040_HAL_Driver/Src/py32f040_hal_pwr.c \
../Middlewares/Third_Party/FreeRTOS/Source/croutine.c \
../Middlewares/Third_Party/FreeRTOS/Source/event_groups.c \
../Middlewares/Third_Party/FreeRTOS/Source/list.c \
../Middlewares/Third_Party/FreeRTOS/Source/queue.c \
../Middlewares/Third_Party/FreeRTOS/Source/stream_buffer.c \
../Middlewares/Third_Party/FreeRTOS/Source/tasks.c \
../Middlewares/Third_Party/FreeRTOS/Source/timers.c \
../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM0/port.c \
../Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c

ASM_SOURCES = startup_py32f040xx.s

######################################
# toolchain
######################################
PREFIX = arm-none-eabi-
ifdef GCC_PATH
CC = $(GCC_PATH)/$(PREFIX)gcc
AS = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP = $(GCC_PATH)/$(PREFIX)objcopy
SZ = $(GCC_PATH)/$(PREFIX)size
else
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
endif
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

######################################
# flags
######################################
CPU = -mcpu=cortex-m0plus
MCU = $(CPU) -mthumb

C_DEFS = \
-DUSE_HAL_DRIVER \
-DPY32F040xB \
-DQAPP_BUILD

C_INCLUDES = \
-I../Inc \
-I../Drivers/BSP/PY32F040xx_Start_Kit \
-I../Drivers/PY32F040_HAL_Driver/Inc \
-I../Drivers/CMSIS/Device/PY32F040/Include \
-I../Drivers/CMSIS/Include \
-I../Middlewares/Third_Party/FreeRTOS/Source/include \
-I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM0

ASFLAGS = $(MCU) $(AS_DEFS) $(OPT) -Wall -fdata-sections -ffunction-sections
CFLAGS  = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections
ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

LDSCRIPT = py32f040xb.ld
LIBS     = -lc -lm -lnosys
LDFLAGS  = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) \
           -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

######################################
# rules
######################################
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	-rm -fR $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/*.d)

# *** EOF ***
