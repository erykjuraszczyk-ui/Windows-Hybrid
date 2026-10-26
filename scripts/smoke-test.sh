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
  -cpu qemu64,-apic \
  -m 512M \
  -cdrom build/hybrid.iso \
  -display none \
  -serial stdio \
  -monitor none \
  -no-reboot \
  -no-shutdown \
  -d int,cpu_reset,guest_errors \
  -D build/qemu-debug.log >"$log_file" 2>&1
qemu_status=$?
set -e
cat "$log_file"
if grep -Fq "FAIL: missing expected boot marker" "$log_file"; then
  echo "---- QEMU CPU/interrupt trace (last 200 lines) ----"
  tail -n 200 build/qemu-debug.log || true
fi
required_markers=(
  "HYBRID: MEMORY_MAP_OK"
  "HYBRID: MEMORY_SELFTEST_OK"
  "HYBRID: FRAMEBUFFER_OK"
  "HYBRID: IDT_OK"
  "HYBRID: EXCEPTION_SELFTEST_OK"
  "HYBRID: TIMER_OK"
  "HYBRID: SCHEDULER_SELFTEST_OK"
  "HYBRID: PREEMPTIVE_SELFTEST_OK"
  "HYBRID: BOOT_OK"
)
for marker in "${required_markers[@]}"; do
  if ! grep -Fq "$marker" "$log_file"; then
    echo "FAIL: missing expected boot marker: $marker (QEMU exit: $qemu_status)." >&2
    exit 1
  fi
done
echo "PASS: Memory Core, IDT, PIT, cooperative scheduler, and timer-driven context switching passed in QEMU."
