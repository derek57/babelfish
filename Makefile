include starlet.mk

CFLAGS = -Wall -Wextra -fpic -mbig-endian -mthumb -march=armv5te -mthumb-interwork -fomit-frame-pointer -Os
LDFLAGS += -fpic -mbig-endian -mthumb -march=armv5t -mthumb-interwork -fomit-frame-pointer
LDSCRIPT = babelfish.ld
LIBS = -lgcc

TARGET = babelfish.elf
TARGET_BIN = babelfish.bin
DATA = vectors.bin.o
OBJS = start.o babelfish.o utils.o ff.o diskio.o sdhc.o string.o gecko.o memory.o memory_asm.o sdmmc.o $(DATA)

include common.mk

all: $(TARGET_BIN)

babelfish.o: babelfish.c vectors.bin.o

%.bin.o: %.bin
	@$(bin2o)

# vectors contains our SWI replacement that outputs debug stuff over USBGecko
vectors.bin: vectors.elf
	$(OBJCOPY) -O binary vectors.elf vectors.bin

vectors.elf: vectors.o
	$(LD) -EB -Ttext=0 -o vectors.elf vectors.o
        
vectors.o: vectors.s
	$(AS) -o vectors.o vectors.s


$(TARGET_BIN): $(TARGET)
	@echo  "  OBJCPY    $@"
	@$(OBJCOPY) -O binary $< $@
	@echo  "  ARMBOOT   armboot.bin"
	@./create_armboot_bin.sh && ./create_armboot_bin
	@-rm -f babelfish.bin vectors.bin

clean: myclean

myclean:
	-rm -f $(TARGET_BIN) vectors.elf vectors.bin vectors_bin.h vectors.o babelfish.map armboot.bin create_armboot_bin

