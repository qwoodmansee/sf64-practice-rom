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

TESTS = test_sd_crc

all: run-all

# Each test source compiles together with its lib dependencies.
test_sd_crc: test_sd_crc.c ../sd_crc.c ../sd_crc.h ../lib_types.h
	$(CC) $(CFLAGS) $(INCLUDES) test_sd_crc.c ../sd_crc.c -o $@

run-all: $(TESTS)
	@for t in $(TESTS); do \
		echo "===> $$t"; \
		./$$t || exit 1; \
	done

clean:
	rm -f $(TESTS)

.PHONY: all run-all clean
