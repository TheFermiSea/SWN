# Makefile by Dan Green <danngreen1@gmail.com>
#

BINARYNAME 		= main

COMBO 			= build/combo
BOOTLOADER_DIR 	= bootloader
BOOTLOADER_HEX 	= bootloader/build/bootloader.hex

FIRMWARE_RELEASE_DIR = LOCAL/Firmwares
FIRMWARE_RELEASE_NAME = SWN_firmware


STARTUP 		= startup_stm32f765xx.s
SYSTEM 			= system_stm32f7xx.c
LOADFILE 		= STM32F765ZGTx_FLASH.ld

DEVICE 			= stm32/device
CORE 			= stm32/core
PERIPH 			= stm32/periph

BUILDDIR 		= build

SOURCES  += $(wildcard $(PERIPH)/src/*.c)
SOURCES  += $(DEVICE)/src/$(STARTUP)
SOURCES  += $(DEVICE)/src/$(SYSTEM)
SOURCES  += $(wildcard src/*.c)
SOURCES  += $(wildcard src/*.cc)
SOURCES  += $(wildcard src/drivers/*.c)
SOURCES  += $(wildcard $(CORE)/src/*.c)
SOURCES  += $(wildcard $(CORE)/src/*.s)

OBJECTS   = $(addprefix $(BUILDDIR)/, $(addsuffix .o, $(sort $(basename $(SOURCES)))))

DEPS = $(OBJECTS:.o=.d)

INCLUDES += -I$(DEVICE)/include \
			-I$(CORE)/include \
			-I$(PERIPH)/include \
			-I inc \
			-I inc/drivers \
			-I inc/tests

ELF 	= $(BUILDDIR)/$(BINARYNAME).elf
HEX 	= $(BUILDDIR)/$(BINARYNAME).hex
BIN 	= $(BUILDDIR)/$(BINARYNAME).bin

ARCH 	= arm-none-eabi
CC 		= $(ARCH)-gcc
CXX		= $(ARCH)-g++
LD 		= $(ARCH)-g++
AS 		= $(ARCH)-as
OBJCPY 	= $(ARCH)-objcopy
OBJDMP 	= $(ARCH)-objdump
GDB 	= $(ARCH)-gdb
SZ 		= $(ARCH)-size

SZOPTS 	= -d

CPU = -mcpu=cortex-m7 
FPU = -mfpu=fpv5-d16
FLOAT-ABI = -mfloat-abi=hard 
MCU = $(CPU) -mthumb -mlittle-endian $(FPU) $(FLOAT-ABI) 

ARCH_CFLAGS = 	-DARM_MATH_CM7 \
				-D'__FPU_PRESENT=1' \
				-DUSE_HAL_DRIVER \
				-DSTM32F765xx

DEBUG ?= 0
ifeq ($(DEBUG), 1)
OPTFLAG = -Og
else
OPTFLAG = -O3
endif

CFLAGS = -g3 -Wall -Wextra \
	-Wdouble-promotion \
	-Werror=return-type \
	-Wno-unused-parameter \
	$(ARCH_CFLAGS) $(MCU) \
	-I. $(INCLUDES) \
	-fno-common \
	-fdata-sections -ffunction-sections \
	# -specs=nano.specs \

DEPFLAGS = -MMD -MP -MF $(BUILDDIR)/$(basename $<).d

CXXFLAGS=$(CFLAGS) \
	-std=c++17 \
	-fno-rtti \
	-fno-exceptions \
	-ffreestanding \
	-Werror=return-type \
	-Wdouble-promotion \
	-Wno-register \

AFLAGS = $(MCU) 

LDSCRIPT = $(DEVICE)/$(LOADFILE)

LFLAGS =  -Wl,-Map,build/main.map,--cref \
	-Wl,--gc-sections \
	$(MCU) \
	-T $(LDSCRIPT)
	# -specs=nano.specs -T $(LDSCRIPT) \

# Per-file optimization override example:
# build/src/hardware_tests.o: OPTFLAG = -O0
# Or use: make DEBUG=1  (sets all files to -Og)


all: Makefile $(BIN) $(HEX)

combo: $(COMBO).hex 
$(COMBO).hex:  $(BOOTLOADER_HEX) $(BIN) $(HEX)
	cat  $(HEX) $(BOOTLOADER_HEX) | \
	awk -f $(BOOTLOADER_DIR)/util/merge_hex.awk > $(COMBO).hex
	$(OBJCPY) -I ihex -O binary $(COMBO).hex $(COMBO).bin


$(BIN): $(ELF)
	$(OBJCPY) -O binary $< $@
	$(OBJDMP) -x --syms $< > $(addsuffix .dmp, $(basename $<))
	ls -l $@ $<

$(HEX): $(ELF)
	$(OBJCPY) --output-target=ihex $< $@
	$(SZ) $(SZOPTS) $(ELF)

$(ELF): $(OBJECTS) 
	@echo "Linking..."
	@$(LD) $(LFLAGS) -o $@ $(OBJECTS)

$(BUILDDIR)/%.o: %.c $(BUILDDIR)/%.d
	@mkdir -p $(dir $@)
	@echo "Compiling $< at $(OPTFLAG)"
	@$(CC) -c $(DEPFLAGS) $(OPTFLAG) $(CFLAGS) $< -o $@

$(BUILDDIR)/%.o: %.cpp $(BUILDDIR)/%.d
	@mkdir -p $(dir $@)
	@echo "Compiling $< at $(OPTFLAG)"
	@$(CXX) -c $(DEPFLAGS) $(OPTFLAG) $(CXXFLAGS) $< -o $@

$(BUILDDIR)/%.o: %.cc $(BUILDDIR)/%.d
	@mkdir -p $(dir $@)
	@echo "Compiling $< at $(OPTFLAG)"
	@$(CXX) -c $(DEPFLAGS) $(OPTFLAG) $(CXXFLAGS) $< -o $@

$(BUILDDIR)/%.o: %.s
	mkdir -p $(dir $@)
	$(AS) $(AFLAGS) $< -o $@ > $(addprefix $(BUILDDIR)/, $(addsuffix .lst, $(basename $<)))

flash: $(BIN)
	st-flash write $(BIN) 0x08010000

clean:
	rm -rf $(BUILDDIR)

%.d: ;

ifneq "$(MAKECMDGOALS)" "clean"
-include $(DEPS)
endif

wav: fsk-wav

fsk-wav: $(BIN)
	export PYTHONPATH='.' && python stm_audio_bootloader/fsk/encoder.py \
		-s 44100 -b 16 -n 8 -z 4 -p 256 -g 16384 -k 1800 \
		$(BIN)

release: wav
	@read -p "Version (example: v2.0): " RELEASEVERSION && \
	mv "$(BUILDDIR)/$(BINARYNAME).wav" "$(FIRMWARE_RELEASE_DIR)/$(FIRMWARE_RELEASE_NAME)_$$RELEASEVERSION.wav" && \
	zip -j "$(FIRMWARE_RELEASE_DIR)/$(FIRMWARE_RELEASE_NAME)_$$RELEASEVERSION.zip" "$(FIRMWARE_RELEASE_DIR)/$(FIRMWARE_RELEASE_NAME)_$$RELEASEVERSION.wav"

