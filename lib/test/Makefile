# Host gcc test runner for lib/ unit tests.
#
# Builds with native compiler (NOT MIPS/IDO) so tests run in seconds on
# the developer machine without needing the N64 toolchain.
#
# Usage from repo root:
#   make lib-test

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Werror -std=c99 -O0 -g
INCLUDES = -I.. -I.

# IMPORTANT: do NOT add -I../fatfs to test builds. Our lib/fatfs/string.h
# is a shim for the IDO build; on the host it would shadow system <string.h>
# and break test code that uses functions outside our 5-decl shim. Tests
# include FatFs headers via "fatfs/ff.h" using the existing -I.. include.

TEST_DISKIO_IMG = /tmp/test_diskio.img

TESTS = test_sd_crc test_serial test_slot_manager test_crc32 test_diskio test_fatfs_smoke test_osk

all: run-all

test_sd_crc: test_sd_crc.c ../sd_crc.c ../sd_crc.h ../lib_types.h
	$(CC) $(CFLAGS) $(INCLUDES) test_sd_crc.c ../sd_crc.c -o $@

test_serial: test_serial.c ../serial.c ../serial.h ../lib_types.h
	$(CC) $(CFLAGS) $(INCLUDES) test_serial.c ../serial.c -o $@

test_slot_manager: test_slot_manager.c ../slot_manager.c ../slot_manager.h ../lib_types.h
	$(CC) $(CFLAGS) $(INCLUDES) test_slot_manager.c ../slot_manager.c -o $@

test_crc32: test_crc32.c ../crc32.c ../crc32.h ../lib_types.h
	$(CC) $(CFLAGS) $(INCLUDES) test_crc32.c ../crc32.c -o $@

test_osk: test_osk.c ../ui/osk.c ../ui/osk.h ../lib_types.h
	$(CC) $(CFLAGS) $(INCLUDES) -DPRACTICE_ROM test_osk.c ../ui/osk.c -o $@

test_diskio: test_diskio.c iodev_mock.c ../fatfs/diskio.c ../fatfs/ff.c ../fatfs/ffunicode.c
	$(CC) $(CFLAGS) $(INCLUDES) \
		test_diskio.c iodev_mock.c \
		../fatfs/diskio.c ../fatfs/ff.c ../fatfs/ffunicode.c \
		-o $@

test_fatfs_smoke: test_fatfs_smoke.c iodev_mock.c ../fatfs/diskio.c ../fatfs/ff.c ../fatfs/ffunicode.c
	$(CC) $(CFLAGS) $(INCLUDES) \
		test_fatfs_smoke.c iodev_mock.c \
		../fatfs/diskio.c ../fatfs/ff.c ../fatfs/ffunicode.c \
		-o $@

# Auto-create a fresh 64 MiB FAT32 test image. macOS uses newfs_msdos
# (built-in); Linux uses mkfs.fat from dosfstools (apt-get install dosfstools).
$(TEST_DISKIO_IMG):
	@echo "Creating fresh 64 MiB FAT32 test image: $@"
	@rm -f $@ $@.dmg
	@if command -v mkfs.fat >/dev/null 2>&1; then \
		dd if=/dev/zero of=$@ bs=1M count=64 2>/dev/null && \
		mkfs.fat -F 32 $@ >/dev/null; \
	elif command -v hdiutil >/dev/null 2>&1; then \
		hdiutil create -size 64m -fs "MS-DOS FAT32" -volname TEST -layout NONE -quiet $@ >/dev/null && \
		mv $@.dmg $@; \
	else \
		echo "ERROR: install dosfstools (mkfs.fat) on Linux, or run on macOS (hdiutil)"; \
		exit 1; \
	fi

run-all: $(TESTS)
	@for t in $(TESTS); do \
		echo "===> $$t"; \
		case "$$t" in \
		  test_sd_crc|test_serial|test_slot_manager|test_crc32|test_osk) ./$$t || exit 1 ;; \
		  *) \
		    rm -f $(TEST_DISKIO_IMG); \
		    $(MAKE) -s $(TEST_DISKIO_IMG); \
		    ./$$t $(TEST_DISKIO_IMG) || exit 1 ;; \
		esac; \
	done

clean:
	rm -f $(TESTS)
	rm -f $(TEST_DISKIO_IMG)

.PHONY: all run-all clean
