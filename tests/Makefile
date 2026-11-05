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
UPDATE_SRC   := ota_update_test.c ../Src/ota_update.c ../Src/ota_frame.c \
                ../Src/crc16.c
RING_SRC     := byte_ring_test.c ../Src/byte_ring.c
CRC32_SRC    := crc32_test.c ../Src/crc32.c
RECORD_SRC   := confirm_record_test.c ../Src/confirm_record.c
OPTION_SRC   := option_plan_test.c ../Src/option_plan.c
ADXL_SRC     := adxl345_test.c ../Src/adxl345.c
REPORT_SRC   := adxl_report_test.c ../Src/adxl_report.c
VIB_SRC      := vibration_test.c ../Src/vibration.c
STREAM_SRC   := vib_telemetry_test.c ../Src/vib_telemetry.c \
                ../Src/vibration.c
ACCEL_CT_SRC := contract_accel_test.c frame_gate.c ../Src/adxl_report.c \
                ../Src/vib_telemetry.c ../Src/vibration.c

TARGETS := contract_frame_test temperature_test fault_test ota_frame_test \
           ota_update_test byte_ring_test crc32_test confirm_record_test \
           option_plan_test adxl345_test adxl_report_test \
           vibration_test vib_telemetry_test wire_probe_test \
           contract_accel_test

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
ota_frame_test: $(OTA_SRC) ../Inc/ota_frame.h ../Inc/ota_frame_parser.h \
                ../Inc/crc16.h
	$(CC) $(CFLAGS) -o $@ $(OTA_SRC)

# The update state machine, with the real encoder and parser on both sides of
# it and a fake board behind its port: flash in RAM, a hand-moved clock.
ota_update_test: $(UPDATE_SRC) ../Inc/ota_update.h ../Inc/ota_frame.h \
                 ../Inc/ota_frame_parser.h ../Inc/le_bytes.h
	$(CC) $(CFLAGS) -o $@ $(UPDATE_SRC)

# The queue between the UART receive interrupt and the main loop. The PC
# cannot race an interrupt, so this checks the index arithmetic it relies on.
byte_ring_test: $(RING_SRC) ../Inc/byte_ring.h ../Inc/ota_frame.h
	$(CC) $(CFLAGS) -o $@ $(RING_SRC)

# The image checksum, anchored to the variant's standard check value.
crc32_test: $(CRC32_SRC) ../Inc/crc32.h
	$(CC) $(CFLAGS) -o $@ $(CRC32_SRC)

# The record that says an image is kept, and every way flash can fail to say
# it: erased, zeroed, corrupted, or left behind by an older image.
confirm_record_test: $(RECORD_SRC) ../Inc/confirm_record.h ../Inc/le_bytes.h
	$(CC) $(CFLAGS) -o $@ $(RECORD_SRC)

# The decision behind the one irreversible write on this chip: refuse unless
# read protection is level 0, change one bit, carry every other bit across.
option_plan_test: $(OPTION_SRC) ../Inc/option_plan.h
	$(CC) $(CFLAGS) -o $@ $(OPTION_SRC)

# The accelerometer, against a fake bus that records every write and
# replays canned reads. The register numbers are spelled out again in the
# test rather than shared, so it checks the driver instead of itself.
adxl345_test: $(ADXL_SRC) ../Inc/adxl345.h
	$(CC) $(CFLAGS) -o $@ $(ADXL_SRC)

# When the status speaks and what it says, against a fake probe that COUNTS
# its calls: "brought up once, never again" is a number here, not a reading
# of the code.
adxl_report_test: $(REPORT_SRC) ../Inc/adxl_report.h
	$(CC) $(CFLAGS) -o $@ $(REPORT_SRC)

# The vibration figure. Every expected value is worked out from the
# definition of root mean square and written as a literal, so the test
# cannot agree with the code by computing it the same way.
vibration_test: $(VIB_SRC) ../Inc/vibration.h
	$(CC) $(CFLAGS) -o $@ $(VIB_SRC)

# The stream on the wire, against a fake queue that COUNTS what was taken,
# so "never more than were waiting" is a number rather than a reading of
# the code. The real vibration maths is linked in, not faked.
vib_telemetry_test: $(STREAM_SRC) ../Inc/vib_telemetry.h
	$(CC) $(CFLAGS) -o $@ $(STREAM_SRC)

# Is a wire physically there? Pure logic with the pin read injected, so
# the fake answers from a table AND records every question, because half
# of what this module gets right is which pull it asks for.
wire_probe_test: wire_probe_test.c ../Src/wire_probe.c ../Inc/wire_probe.h
	$(CC) $(CFLAGS) -DBRINGUP_DIAGNOSTIC=1 -o $@ wire_probe_test.c \
	    ../Src/wire_probe.c

# The two accelerometer frames against the HOST's acceptance rules, with the
# real modules producing the bytes rather than a stand-in.
contract_accel_test: $(ACCEL_CT_SRC) frame_gate.h ../Inc/adxl_report.h \
                    ../Inc/vib_telemetry.h
	$(CC) $(CFLAGS) -o $@ $(ACCEL_CT_SRC)

# Every target, from the one list, rather than a second list beside it.
# Until 2026-11-05 this rule named each binary by hand, so a test added to
# TARGETS was BUILT and never RUN, and the suite still said green. A test
# that nothing executes is worse than no test, because it reads like
# coverage. One list, and adding a target is enough.
run: $(TARGETS)
	@for t in $(TARGETS); do ./$$t || exit 1; done

clean:
	rm -f $(TARGETS) $(addsuffix .exe,$(TARGETS))
