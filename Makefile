HOST_CC   ?= gcc
TARGET_CC ?= gcc
OBJCOPY   ?= objcopy

# =========================================================================
# СПИСОК ПРИЛОЖЕНИЙ
# Имена файлов без расширения .c (они должны лежать в папке userland/)
APPS = init pc psh echo edit clear pwd ls cat touch mkdir rm pexdump cp mv sudo	whoami reboot wm explorer term notepad calc sysmon
# ========================================================================= 

PEX_FLAGS_sudo = 0x0004	
PEX_FLAGS_pt = 0x0005	

PEX_HEAP_SIZE_pt = 131072
PEX_HEAP_SIZE_wm = 262144

# Путь к директории корня виртуальной ФС ядра
DEST_FS_ROOT = /mnt/d/gphu/kernel/datslaf/root/bin

BUILD_DIR = build
USER_DIR  = userland
HDR_DIR   = header
UHDR_DIR   = ul_includes
SRC_DIR   = src

TARGET_CFLAGS = -m32 -march=i486 -ffreestanding -fno-pie -fno-stack-protector \
                -fno-asynchronous-unwind-tables -fno-unwind-tables \
                -fcf-protection=none -nostdinc -nostdlib -O1 -Wall \
                -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -fno-builtin \
                -I$(USER_DIR) -I$(HDR_DIR) -I$(UHDR_DIR)
				
TARGET_LDFLAGS = -m32 -nostdlib -Wl,--build-id=none -T $(USER_DIR)/linker.ld

# Общие объекты, которые линкуются с каждым приложением
SHARED_OBJS = $(BUILD_DIR)/entry.o $(BUILD_DIR)/photon.o $(BUILD_DIR)/ulibc.o

# Генерируем пути к финальным PEX-файлам и LCL-исполняемым
PEX_TARGETS = $(patsubst %, $(BUILD_DIR)/%, $(APPS))
LCL_TARGETS = $(patsubst %, %_lcl, $(APPS))

all: $(BUILD_DIR)/pex_builder $(PEX_TARGETS)

lcl: $(LCL_TARGETS)

# Сборщик хостовой утилиты
$(BUILD_DIR)/pex_builder: $(SRC_DIR)/pex_builder.c $(HDR_DIR)/pex_header.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -O2 -I$(HDR_DIR) $< -o $@

$(BUILD_DIR)/pex_stats: $(SRC_DIR)/pex_stats.c $(HDR_DIR)/pex_header.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -O2 -I$(HDR_DIR) $< -o $@


stats: $(BUILD_DIR)/pex_stats
	@./$(BUILD_DIR)/pex_stats $(PEX_TARGETS)

# Компиляция общих библиотек (entry.S, photon.c, ulibc.c)
$(BUILD_DIR)/entry.o: $(USER_DIR)/entry.S
	@mkdir -p $(BUILD_DIR)
	$(TARGET_CC) $(TARGET_CFLAGS) -c $< -o $@

$(BUILD_DIR)/photon.o: $(USER_DIR)/photon.c $(USER_DIR)/photon.h
	@mkdir -p $(BUILD_DIR)
	$(TARGET_CC) $(TARGET_CFLAGS) -c $< -o $@

$(BUILD_DIR)/ulibc.o: $(UHDR_DIR)/ulibc.c $(UHDR_DIR)/ulibc.h $(USER_DIR)/photon.h
	@mkdir -p $(BUILD_DIR)
	$(TARGET_CC) $(TARGET_CFLAGS) -c $< -o $@

# Шаблон компиляции любого приложения (*.c -> *.o)
$(BUILD_DIR)/%.o: $(USER_DIR)/%.c $(USER_DIR)/photon.h $(UHDR_DIR)/ulibc.h
	@mkdir -p $(BUILD_DIR)
	$(TARGET_CC) $(TARGET_CFLAGS) -c $< -o $@

# Шаблон линковки любого приложения в ELF
$(BUILD_DIR)/%.elf: $(SHARED_OBJS) $(BUILD_DIR)/%.o
	@mkdir -p $(BUILD_DIR)
	$(TARGET_CC) $(TARGET_LDFLAGS) $^ -o $@

# Шаблон извлечения сырого бинарника из ELF
$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	@mkdir -p $(BUILD_DIR)
	$(OBJCOPY) -O binary $< $@

# Шаблон сборки финального PEX-файла и копирования в VFS
$(BUILD_DIR)/%: $(BUILD_DIR)/%.elf $(BUILD_DIR)/%.bin $(BUILD_DIR)/pex_builder
	@mkdir -p $(BUILD_DIR)
	./$(BUILD_DIR)/pex_builder $(BUILD_DIR)/$*.elf $(BUILD_DIR)/$*.bin $@ 16384 $(if $(PEX_HEAP_SIZE_$*),$(PEX_HEAP_SIZE_$*),0) $(if $(PEX_FLAGS_$*),$(PEX_FLAGS_$*),0)
	@if [ -d "$(DEST_FS_ROOT)" ]; then \
		rm -f $(DEST_FS_ROOT)/$*; \
		cp $@ $(DEST_FS_ROOT)/; \
		echo "[DEPLOY] Copied $@ to $(DEST_FS_ROOT)/$*"; \
	fi

# Шаблон сборки LCL (Linux Compatibility Layer)
%_lcl: $(USER_DIR)/%.c $(USER_DIR)/lcl.c $(UHDR_DIR)/ulibc.c
	$(HOST_CC) $^ -o $@ -O2 -Wall -I$(USER_DIR) -I$(HDR_DIR)

clean:
	rm -rf $(BUILD_DIR) $(LCL_TARGETS)

.PHONY: all lcl stats clean