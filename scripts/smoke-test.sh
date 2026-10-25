#!/usr/bin/env bash
set -euo pipefail

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
  echo "ERROR: qemu-system-x86_64 is required for the boot smoke test." >&2
  exit 2
fi
if ! command -v timeout >/dev/null 2>&1; then
  echo "ERROR: timeout (coreutils) is required for the boot smoke test." >&2
  exit 2
fi

mkdir -p build
log_file="build/qemu-serial.log"
set +e
timeout 12s qemu-system-x86_64 \
  -machine q35 \
  -m 512M \
  -cdrom build/hybrid.iso \
  -display none \
  -serial stdio \
  -monitor none \
  -no-reboot \
  -no-shutdown >"$log_file" 2>&1
qemu_status=$?
set -e

cat "$log_file"
if grep -Fq "HYBRID: BOOT_OK" "$log_file"; then
  echo "PASS: QEMU handed control to the HYBRID kernel."
  exit 0
fi

echo "FAIL: the expected kernel boot marker was not found (QEMU exit: $qemu_status)." >&2
exit 1
