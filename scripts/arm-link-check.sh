#!/bin/sh
# The two ARM gates from .github/workflows/ci.yml, run locally.
#
# WHY THIS EXISTS. The delivery checklist lists a real link for Cortex-M4 and
# the oversized image that must FAIL to link. Neither could be run on this
# machine, because arm-none-eabi-gcc is not installed, so every piece was
# delivered with those two gates declared as not run. They were not missing:
# STM32CubeIDE ships the toolchain it builds with, and nobody had looked.
#
# The path carries a CubeIDE version, a toolchain version and a build stamp,
# so it is FOUND rather than written down. A path written down is the defect
# this workspace has now paid for three times in two days.
#
# Usage: sh scripts/arm-link-check.sh

set -e

bin=""
for dir in /c/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools-for-stm32*/tools/bin; do
    [ -x "$dir/arm-none-eabi-gcc.exe" ] && bin="$dir"
done

if [ -z "$bin" ]; then
    echo "arm-link-check: no arm-none-eabi-gcc under any STM32CubeIDE install." >&2
    echo "Looked in /c/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools-for-stm32*" >&2
    exit 2
fi

PATH="$bin:$PATH"
export PATH
arm-none-eabi-gcc --version | head -1

# The same flags as the workflow, which are the same flags CubeIDE uses. A
# green run here means the real build is green, with one honest limit: the
# runner installs Ubuntu's gcc-arm-none-eabi while this is ST's build, so a
# pass here is evidence rather than proof. The same caveat the checklist
# already carries about clang-tidy.
FLAGS="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
       -std=gnu11 -Wall -Wextra -Werror -O1 -IInc
       --specs=nano.specs --specs=nosys.specs -T STM32L476RGTX_FLASH.ld"

out="${TMPDIR:-/tmp}"

echo "== a real link"
# shellcheck disable=SC2086
arm-none-eabi-gcc $FLAGS Src/*.c Startup/startup_stm32l476rgtx.s -o "$out/firmware.elf"
arm-none-eabi-size "$out/firmware.elf"

echo "== an image larger than one bank must NOT link"
# shellcheck disable=SC2086
if arm-none-eabi-gcc $FLAGS Src/*.c Startup/startup_stm32l476rgtx.s \
       tests/oversize_image.c -o "$out/oversize.elf" 2> "$out/oversize.log"; then
    echo "FAILED: an oversized image linked, so the FLASH limit is gone." >&2
    exit 1
fi
grep -q "region .FLASH. overflowed" "$out/oversize.log" || {
    echo "FAILED: the link failed for some other reason:" >&2
    cat "$out/oversize.log" >&2
    exit 1
}
grep -o "region .FLASH. overflowed by [0-9]* bytes" "$out/oversize.log" | head -1

echo "Both ARM gates pass."
