# Host build for the tests. Nothing here goes near the board: it compiles the
# pure-logic sources with a normal desktop compiler, so
# `mingw32-make -C tests run` is a red/green check on the wire protocol, the
# conversion maths, the fault blink patterns and the UART flash protocol.
#
# No test framework and no package to install: the toolchain is whatever
# clang or gcc is already on PATH.
#
# CC is whatever make already defaults to (cc). Override on the command line
# if you want a specific one: `mingw32-make -C tests run CC=clang`.
CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -O1 -I../Inc -I.

CONTRACT_SRC := contract_frame_test.c frame_gate.c ../Src/telemetry.c
TEMP_SRC     := temperature_test.c ../Src/temperature.c
FAULT_SRC    := fault_test.c ../Src/fault.c
OTA_SRC      := ota_frame_test.c ../Src/ota_frame.c ../Src/crc16.c

TARGETS := contract_frame_test temperature_test fault_test ota_frame_test

.PHONY: all run clean

all: $(TARGETS)

# telemetry only needs the TEMPERATURE_INVALID constant from temperature.h,
# so there is nothing from temperature.c to link here.
contract_frame_test: $(CONTRACT_SRC) frame_gate.h ../Inc/telemetry.h \
                     ../Inc/temperature.h
	$(CC) $(CFLAGS) -o $@ $(CONTRACT_SRC)

temperature_test: $(TEMP_SRC) ../Inc/temperature.h
	$(CC) $(CFLAGS) -o $@ $(TEMP_SRC)

# board.c is NOT linked here. It exists only to write to fixed register
# addresses, which means nothing on a PC. The two board symbols that fault.c
# references are stubbed inside fault_test.c instead, so the link stays honest
# about what is and is not present.
fault_test: $(FAULT_SRC) ../Inc/fault.h
	$(CC) $(CFLAGS) -o $@ $(FAULT_SRC)

# The UART flash protocol envelope. Pure logic, like telemetry, so the whole
# parser runs here: the anchors are copied from industrial-hmi's agreed spec.
ota_frame_test: $(OTA_SRC) ../Inc/ota_frame.h ../Inc/crc16.h
	$(CC) $(CFLAGS) -o $@ $(OTA_SRC)

run: $(TARGETS)
	./contract_frame_test
	./temperature_test
	./fault_test
	./ota_frame_test

clean:
	rm -f $(TARGETS) $(addsuffix .exe,$(TARGETS))
