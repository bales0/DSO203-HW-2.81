# Wildcat V6.6, hardware 2.81 and later. Run make from this directory.
.DEFAULT_GOAL := all
CROSS ?= arm-none-eabi-
CC = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
SIZE = $(CROSS)size

BUILD := build
BINDIR := ../bin/WildcatV6_6
CPUFLAGS := -mcpu=cortex-m3 -mthumb -msoft-float
CPPFLAGS += -I. -Iinc -IFWLib/inc
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu90 -Wall -Werror -fno-common -fzero-initialized-in-bss $(CPUFLAGS)
ASFLAGS += $(CPUFLAGS)
LDFLAGS += $(CPUFLAGS) -nostartfiles -Wl,-Map,$(BUILD)/app1.map
LDLIBS += -lc

C_SOURCES := Fft144.c UiCommon.c UI144.c UnifiedCal.c Calibrat.c Draw.c Files.c Files144.c Function.c Interrupt.c Main.c Menu.c Process.c NativeWindow.c Digital144.c FpgaProfile.c Interlace144.c Phase144.c Capture144.c CaptureWindow.c ViewWindowGeometry.c Frame144.c Scope144.c Wildcat144.c startup.c stm32f10x_nvic.c
OBJECTS := $(addprefix $(BUILD)/,$(C_SOURCES:.c=.o)) $(BUILD)/BIOS.o $(BUILD)/cortexm3_macro.o
DEPS := $(OBJECTS:.o=.d)

.PHONY: all clean size
.DELETE_ON_ERROR:
all: $(BINDIR)/app1.hex \
	$(BINDIR)/APP1A.BIN $(BINDIR)/APP1A.ADR \
	$(BINDIR)/APP1B.BIN $(BINDIR)/APP1B.ADR \
	$(BINDIR)/APP1C.BIN $(BINDIR)/APP1C.ADR size

$(BUILD):
	mkdir "$(BUILD)"

../bin:
ifeq ($(OS),Windows_NT)
	mkdir "..\bin"
else
	mkdir "../bin"
endif

$(BINDIR): | ../bin
ifeq ($(OS),Windows_NT)
	mkdir "..\bin\WildcatV6_6"
else
	mkdir "$(BINDIR)"
endif

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.s | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/app1.elf: $(OBJECTS) app1.lds main.lds
	$(CC) $(LDFLAGS) -T app1.lds -o $@ $(OBJECTS) $(LDLIBS)

$(BINDIR)/app1.hex: $(BUILD)/app1.elf | $(BINDIR)
	$(OBJCOPY) -O ihex $< $@

# The application has three disjoint flash regions. A single flat BIN would
# overwrite the firmware between them, so emit one ADR/BIN pair per region.
$(BINDIR)/APP1A.BIN: $(BUILD)/app1.elf Makefile | $(BINDIR)
	$(OBJCOPY) -O binary -j .isr_vector -j ".ARM.extab*" -j ".ARM.exidx*" -j .data $< $@

$(BINDIR)/APP1B.BIN: $(BUILD)/app1.elf | $(BINDIR)
	$(OBJCOPY) -O binary -j .spare $< $@

$(BINDIR)/APP1C.BIN: $(BUILD)/app1.elf | $(BINDIR)
	$(OBJCOPY) -O binary -j .extra $< $@

$(BINDIR)/APP1A.ADR: | $(BINDIR)
	echo 0x0800C000 > $@

$(BINDIR)/APP1B.ADR: | $(BINDIR)
	echo 0x0802C000 > $@

$(BINDIR)/APP1C.ADR: | $(BINDIR)
	echo 0x08047000 > $@

size: $(BUILD)/app1.elf
	$(SIZE) $<

# Remove intermediates and generated deliverables.
ifeq ($(OS),Windows_NT)
clean:
	cmd /C "if exist build del /Q build\*.o build\*.d build\*.elf build\*.map build\*.hex build\*.BIN build\*.ADR"
	cmd /C "if exist ..\bin\WildcatV6_6 del /Q ..\bin\WildcatV6_6\app1.hex ..\bin\WildcatV6_6\*.BIN ..\bin\WildcatV6_6\*.ADR"
else
clean:
	rm -f -- $(OBJECTS) $(DEPS) $(BUILD)/app1.elf $(BUILD)/app1.map $(BUILD)/app1.hex $(BUILD)/APP1A.BIN $(BUILD)/APP1A.ADR $(BUILD)/APP1B.BIN $(BUILD)/APP1B.ADR $(BUILD)/APP1C.BIN $(BUILD)/APP1C.ADR $(BINDIR)/app1.hex $(BINDIR)/APP1A.BIN $(BINDIR)/APP1A.ADR $(BINDIR)/APP1B.BIN $(BINDIR)/APP1B.ADR $(BINDIR)/APP1C.BIN $(BINDIR)/APP1C.ADR
endif

-include $(DEPS)
