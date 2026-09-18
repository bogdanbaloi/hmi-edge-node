# Host build for the contract test. Nothing here goes near the board: it
# compiles the pure-logic Src/telemetry.c with a normal desktop compiler, so
# `mingw32-make -C tests run` is a red/green check on the wire protocol.
#
# No test framework and no package to install: the toolchain is whatever
# clang or gcc is already on PATH.

# CC is whatever make already defaults to (cc). Override on the command line
# if you want a specific one: `mingw32-make -C tests run CC=clang`.
CFLAGS  := -std=c11 -Wall -Wextra -Werror -pedantic -O1 -I../Inc -I.
TARGET  := contract_frame_test
SOURCES := contract_frame_test.c frame_gate.c ../Src/telemetry.c

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SOURCES) frame_gate.h ../Inc/telemetry.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe
