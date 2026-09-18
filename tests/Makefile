# Host build for the tests. Nothing here goes near the board: it compiles the
# pure-logic sources (Src/telemetry.c, Src/temperature.c) with a normal
# desktop compiler, so `mingw32-make -C tests run` is a red/green check on
# both the wire protocol and the conversion maths.
#
# No test framework and no package to install: the toolchain is whatever
# clang or gcc is already on PATH.
#
# CC is whatever make already defaults to (cc). Override on the command line
# if you want a specific one: `mingw32-make -C tests run CC=clang`.
CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -O1 -I../Inc -I.

CONTRACT_SRC := contract_frame_test.c frame_gate.c ../Src/telemetry.c
TEMP_SRC     := temperature_test.c ../Src/temperature.c

TARGETS := contract_frame_test temperature_test

.PHONY: all run clean

all: $(TARGETS)

# telemetry only needs the TEMPERATURE_INVALID constant from temperature.h,
# so there is nothing from temperature.c to link here.
contract_frame_test: $(CONTRACT_SRC) frame_gate.h ../Inc/telemetry.h \
                     ../Inc/temperature.h
	$(CC) $(CFLAGS) -o $@ $(CONTRACT_SRC)

temperature_test: $(TEMP_SRC) ../Inc/temperature.h
	$(CC) $(CFLAGS) -o $@ $(TEMP_SRC)

run: $(TARGETS)
	./contract_frame_test
	./temperature_test

clean:
	rm -f $(TARGETS) $(addsuffix .exe,$(TARGETS))
