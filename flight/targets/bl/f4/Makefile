###############################################################################
# @file       Makefile
# @author     The OpenPilot Team, http://www.openpilot.org, Copyright (C) 2009.
# @author     Tau Labs, http://taulabs.org, Copyright (C) 2012-2013
# @author     dRonin, http://dRonin.org/, Copyright (C) 2016
#
# @addtogroup 
# @{
# @addtogroup 
# @{
# @brief Makefile to build bootloader for STM32F4 boards
###############################################################################
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <http://www.gnu.org/licenses/>
# 
# Additional note on redistribution: The copyright and license notices above
# must be maintained in each individual source file that is a derivative work
# of this source file; otherwise redistribution is prohibited.

ifndef MAKE_INC_DIR
$(error This Makefile cannot be run directly)
endif
include $(MAKE_INC_DIR)/firmware-defs.mk

# Paths
PIOSINC = $(PIOS)/inc
PIOSCOMMON = $(PIOS)/Common
PIOSCOMMONLIB = $(PIOSCOMMON)/Libraries
FLIGHTLIBINC = $(FLIGHTLIB)/inc

# List C source files here. (C dependencies are automatically generated.)
# use file-extension c for "c-only"-files

## Bootloader (architecture specific)
SRC += $(BLCOMMONDIR)/main.c
SRC += $(BLCOMMONDIR)/led_pwm.c
SRC += $(BLCOMMONDIR)/bl_xfer.c

## Bootloader (board specific implementations)
SRC += $(BLBOARDDIR)/pios_board.c
SRC += $(PIOS)/STM32/pios_usb_board_data.c

## PIOS Hardware (STM32F4xx)
PIOS_DEVLIB			:=	$(PIOS)/STM32F4xx
include $(PIOS_DEVLIB)/library.mk
SRC += pios_bl_helper.c
SRC += pios_exti.c
SRC += pios_flash_internal.c
SRC += pios_iap.c
SRC += pios_annunc.c
SRC += pios_rtc.c
SRC += pios_sys.c
SRC += pios_tim.c
SRC += pios_ws2811.c
SRC += pios_usb.c
SRC += pios_usb_cdc.c
SRC += pios_usb_hid.c
SRC += pios_usbhook.c
SRC += startup.c
SRC += vectors_stm32f4xx.c

ifneq "$(findstring STM32F40_41xxx,$(STM32_TYPE))" ""
LINKER_SCRIPTS_BL	 =	$(PIOS_DEVLIB)/link_STM32F4xx_BL_memory.ld \
                        $(PIOS_DEVLIB)/link_STM32F4xx_sections.ld
else ifneq "$(findstring STM32F446xx,$(STM32_TYPE))" ""
LINKER_SCRIPTS_BL	 =	$(PIOS_DEVLIB)/link_STM32F446xx_BL_memory.ld \
                        $(PIOS_DEVLIB)/link_STM32F446xx_sections.ld
else
$(error No linker script found for $(STM32_TYPE))
endif

## PIOS Hardware (Common)
SRC += pios_delay.c
SRC += pios_board_info.c
SRC += pios_com_msg.c
SRC += printf-stdarg.c
SRC += pios_usb_desc_hid_only.c
SRC += pios_usb_util.c
SRC += pios_flash.c
SRC += pios_flash_jedec.c
SRC += pios_heap.c
SRC += pios_semaphore.c
SRC += pios_spi.c
SRC += pios_irq.c
SRC += pios_wdg.c

# List any extra directories to look for include files here.
#    Each directory must be seperated by a space.
EXTRAINCDIRS  += $(PIOS)
EXTRAINCDIRS  += $(PIOSINC)
EXTRAINCDIRS  += $(FLIGHTLIBINC)
EXTRAINCDIRS  += $(STMSPDINCDIR)
EXTRAINCDIRS  += $(BOARD_INFO_DIR)
EXTRAINCDIRS  += $(BLBOARDDIR)
EXTRAINCDIRS  += $(BLARCHDIR)
EXTRAINCDIRS  += $(BLCOMMONDIR)

# Place project-specific -D (define) and/or 
# -U options for C here.
CDEFS += -DSYSCLK_FREQ=$(SYSCLK_FREQ)
CDEFS += -DUSE_$(BOARD)

# Provide (only) the bootloader with board-specific defines
BLONLY_CDEFS += -DBOARD_TYPE=$(BOARD_TYPE)
BLONLY_CDEFS += -DBOARD_REVISION=$(BOARD_REVISION)
BLONLY_CDEFS += -DHW_TYPE=$(HW_TYPE)
BLONLY_CDEFS += -DBOOTLOADER_VERSION=$(BOOTLOADER_VERSION)
BLONLY_CDEFS += -DFW_BANK_BASE=$(FW_BANK_BASE)
BLONLY_CDEFS += -DFW_BANK_SIZE=$(FW_BANK_SIZE)
BLONLY_CDEFS += -DFW_DESC_BASE=$(FW_DESC_BASE)
BLONLY_CDEFS += -DFW_DESC_SIZE=$(FW_DESC_SIZE)
BLONLY_CDEFS += -DEE_BANK_BASE=$(EE_BANK_BASE)
BLONLY_CDEFS += -DEE_BANK_SIZE=$(EE_BANK_SIZE)

# Place project-specific -D and/or -U options for 
# Assembler with preprocessor here.
ADEFS = -D__ASSEMBLY__

#-----

# Compiler flags.

#  -g*:          generate debugging information
#  -O*:          optimization level
#  -f...:        tuning, see GCC manual and avr-libc documentation
#  -Wall...:     warning level
#  -Wa,...:      tell GCC to pass this to the assembler.
#    -adhlns...: create assembler listing
#
# Flags for C and C++ (arm-elf-gcc/arm-elf-g++)
CFLAGS += -D$(STM32_TYPE)

CFLAGS += -gdwarf-2
CFLAGS += -Os -fconserve-stack
CFLAGS += -fdata-sections -ffunction-sections

CFLAGS += -mcpu=$(MCU)
CFLAGS += $(CDEFS)
CFLAGS += $(BLONLY_CDEFS)
CFLAGS += $(patsubst %,-I%,$(EXTRAINCDIRS)) -I.

CFLAGS += -mapcs-frame 
CFLAGS += -fomit-frame-pointer

CFLAGS += -Wall
CFLAGS += -Werror
CFLAGS += -Wa,-adhlns=$(addprefix $(OUTDIR)/, $(notdir $(addsuffix .lst, $(basename $<))))
# Compiler flags to generate dependency files:
CFLAGS += -MD -MP -MF $(OUTDIR)/dep/$(@F).d

CFLAGS += -DBOOTLOADER
# flags only for C
CONLYFLAGS += -std=gnu99

# Assembler flags.
#  -Wa,...:    tell GCC to pass this to the assembler.
#  -ahlns:     create listing
ASFLAGS  = -mcpu=$(MCU) -I. -x assembler-with-cpp
ASFLAGS += $(ADEFS)
ASFLAGS += -Wa,-adhlns=$(addprefix $(OUTDIR)/, $(notdir $(addsuffix .lst, $(basename $<))))
ASFLAGS += $(patsubst %,-I%,$(EXTRAINCDIRS))

# Linker flags.
#  -Wl,...:     tell GCC to pass this to linker.
#    -Map:      create map file
#    --cref:    add cross reference to  map file
LDFLAGS = -nostartfiles -Wl,-Map=$(OUTDIR)/$(TARGET).map,--cref,--gc-sections
LDFLAGS += -Wl,-static
LDFLAGS += -lc_nano -lgcc 
LDFLAGS += -Wl,--warn-common
LDFLAGS += -Wl,--fatal-warnings

# Linker scripts
LDFLAGS += $(addprefix -T,$(LINKER_SCRIPTS_BL))

# List of all source files.
ALLSRC     = $(ASRC) $(SRC)
# List of all source files without directory and file-extension.
ALLSRCBASE = $(notdir $(basename $(ALLSRC)))

# Define all object files.
ALLOBJ     = $(addprefix $(OUTDIR)/, $(addsuffix .o, $(ALLSRCBASE)))

# Link: create ELF output file from object files.
$(eval $(call LINK_TEMPLATE, $(OUTDIR)/$(TARGET).elf, $(ALLOBJ)))

# Assemble: create object files from assembler source files.
$(foreach src, $(ASRC), $(eval $(call ASSEMBLE_TEMPLATE, $(src)))) 

# Compile: create object files from C source files.
$(foreach src, $(SRC), $(eval $(call COMPILE_C_TEMPLATE, $(src)))) 

# Compile: create assembler files from C source files. ARM/Thumb
$(eval $(call PARTIAL_COMPILE_TEMPLATE, SRC))

# Add jtag targets (program and wipe)
$(eval $(call JTAG_TEMPLATE,$(OUTDIR)/$(TARGET).bin,$(BL_BANK_BASE),$(BL_BANK_SIZE),$(OPENOCD_JTAG_CONFIG),$(OPENOCD_CONFIG)))

.PHONY: hex bin
hex: $(OUTDIR)/$(TARGET).hex
bin: $(OUTDIR)/$(TARGET).bin

# Display sizes of sections.
$(eval $(call SIZE_TEMPLATE, $(OUTDIR)/$(TARGET).elf))

# Generate Doxygen documents
docs:
	doxygen  $(DOXYGENDIR)/doxygen.cfg

# Create output files directory
# all known MS Windows OS define the ComSpec environment variable
ifdef ComSpec
$(shell md $(subst /,\\,$(OUTDIR)) 2>NUL)
else
$(shell mkdir -p $(OUTDIR) 2>/dev/null)
endif

# Include the dependency files.
ifdef ComSpec
-include $(shell md $(subst /,\\,$(OUTDIR))\dep 2>NUL) $(wildcard $(OUTDIR)/dep/*)
else
-include $(shell mkdir -p $(OUTDIR) 2>/dev/null) $(shell mkdir $(OUTDIR)/dep 2>/dev/null) $(wildcard $(OUTDIR)/dep/*)
endif
