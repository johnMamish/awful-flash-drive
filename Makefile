#directories
OBJ_DIR = obj
OUTPUT_DIR = build
OUTPUT = build

LIB_SAMD21 = ./samd21/include
LIB_CMSIS =  ./CMSIS/Include
PROJECT_INCLUDES = ./inc
INCLUDES = -I. -I$(LIB_SAMD21) -I$(LIB_CMSIS) -I$(PROJECT_INCLUDES)

C_SOURCES = $(shell find . -name "*.c" ! -iname ".*")
ASM_SOURCES = $(shell find . -name "*.S" ! ! -iname ".*")

C_OBJECTS = $(addprefix $(OBJ_DIR)/, $(notdir $(C_SOURCES:.c=.c.o)))
ASM_OBJECTS = $(addprefix $(OBJ_DIR)/, $(notdir $(ASM_SOURCES:.S=.S.o)))


LINKER_SCRIPT = ./samd21j18a_flash.ld

DEPENDENCIES_FILE = dependencies.d
#Tool prefix for cross-compilation
CROSS_COMPILE = arm-none-eabi-

#Compilation tools
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
SIZE = $(CROSS_COMPILE)size
STRIP = $(CROSS_COMPILE)strip
OBJCOPY = $(CROSS_COMPILE)objcopy
GDB = $(CROSS_COMPILE)gdb
NM = $(CROSS_COMPILE)nm

####################
#    gcc flags     #
####################
# Warnings / errors
CFLAGS += -Wall -Werror -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-function -Wno-missing-braces

# ARM stuff
CFLAGS += -march=armv6-m -mthumb -mno-thumb-interwork -mtune=cortex-m0plus

# Linker script stuff
CFLAGS += -ffunction-sections

OPTIMIZATION = -O3
CFLAGS += --std=gnu99 $(OPTIMIZATION) -g

#define samd21j18a
CFLAGS += -D __SAMD21J18A__

#includes
CFLAGS += $(INCLUDES)

#####################
#  assembler flags  #
#####################
ASFLAGS += -mfloat-abi=soft -march=armv6-m -mcpu=cortex-m0plus -Wall -g $(OPTIMIZATION) -mthumb
ASFLAGS += -mlong-calls
ASFLAGS += $(INCLUDES) -D__ASSEMBLY__ -D $(CHIP) -D $(FAMILY)

#####################
#   linker flags    #
#####################
LDFLAGS += -Wl,-nostdlib -Wl,-lgcc
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,--unresolved-symbols=report-all -Wl,--warn-common
LDFLAGS += -Wl,--warn-section-align
LDFLAGS += -mcpu=cortex-m0plus -mthumb

all: directories dependencies
	@$(MAKE) $(OUTPUT_DIR)/$(OUTPUT).elf

$(OUTPUT_DIR)/$(OUTPUT).elf: $(C_OBJECTS) $(ASM_OBJECTS)
	@echo "[$@]"
	@$(CC) $(LDFLAGS) $(LD_OPTIONAL) -T$(LINKER_SCRIPT) -Wl,-Map,$(OUTPUT_DIR)/$(OUTPUT).map -o $(OUTPUT_DIR)/$(OUTPUT).elf $^
	@$(NM) $(OUTPUT_DIR)/$(OUTPUT).elf > $(OUTPUT_DIR)/$(OUTPUT).elf.txt
	@$(SIZE) $^ $(OUTPUT_DIR)/$(OUTPUT).elf

directories:
	@mkdir -p $(OUTPUT_DIR);
	@mkdir -p $(OBJ_DIR);

dependencies:
	@$(CC) -MM $(C_SOURCES) $(CFLAGS) $(INCLUDES) | sed 's|\([a-zA-Z0-9_-]*\)\.o|$(OBJ_DIR)/\1.c.o|' > $(DEPENDENCIES_FILE)

$(OBJ_DIR)/%.c.o:
	@echo "[$<]"
	@$(CC) $(CFLAGS) -S -o $(OBJ_DIR)/$(notdir $@).s $<
	@$(CC) $(CFLAGS) -c -o $(OBJ_DIR)/$(notdir $@)  $<

$(OBJ_DIR)/%.S.o:
	@echo "[$<]"
	@$(CC) $(ASFLAGS) -c -o $(OBJ_DIR)/$(notdir $@) $<

-include $(DEPENDENCIES_FILE)

clean:
	@echo removing all build files
	@find . -name "*.o" -exec rm -f {} \;
	@find . -name "*.o.*" -exec rm -f {} \;
	@find . -name "*.elf" -exec rm -f {} \;
	@find . -name "*.out" -exec rm -f {} \;
	@find . -name "*.map" -exec rm -f {} \;
	@rm -rf $(OUTPUT_DIR)
	@rm -rf $(OBJ_DIR)
	@echo done

.PHONY: all directories dependencies clean
