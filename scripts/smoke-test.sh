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
required_markers=(
  "HYBRID: MEMORY_MAP_OK"
  "HYBRID: MEMORY_SELFTEST_OK"
  "HYBRID: FRAMEBUFFER_OK"
  "HYBRID: TSS_OK"
  "HYBRID: IDT_OK"
  "HYBRID: IST_OK"
  "HYBRID: EXCEPTION_SELFTEST_OK"
  "HYBRID: TIMER_OK"
  "HYBRID: SCHEDULER_SELFTEST_OK"
  "HYBRID: TASK_LIFECYCLE_SELFTEST_OK"
  "HYBRID: PREEMPTIVE_SELFTEST_OK"
  "HYBRID: BLOCKING_SYNC_SELFTEST_OK"
  "HYBRID: TIMER_TIMEOUT_CANCEL_SELFTEST_OK"
  "HYBRID: SEMAPHORE_IPC_SELFTEST_OK"
  "HYBRID: IPC_SERVICES_SELFTEST_OK"
  "HYBRID: IPC_REQUEST_RESPONSE_SELFTEST_OK"
  "HYBRID: IPC_RUNTIME_SELFTEST_OK"
  "HYBRID: IPC_LIFECYCLE_SELFTEST_OK"
  "HYBRID: IPC_BACKPRESSURE_SELFTEST_OK"
  "HYBRID: IPC_RECEIPTS_RETRIES_SELFTEST_OK"
  "HYBRID: IPC_TIMER_RETRY_DEAD_LETTER_SELFTEST_OK"
  "HYBRID: IPC_ADAPTIVE_LOAD_SELFTEST_OK"
  "HYBRID: IPC_CIRCUIT_BREAKER_SELFTEST_OK"
  "HYBRID: IPC_TRACE_SELFTEST_OK"
  "HYBRID: IPC_RATE_LIMIT_SELFTEST_OK"
  "HYBRID: IPC_FAIR_ADMISSION_SELFTEST_OK"
  "HYBRID: IPC_DEADLINE_QOS_SELFTEST_OK"
  "HYBRID: IPC_EXPIRATION_SELFTEST_OK"
  "HYBRID: IPC_QUEUE_TELEMETRY_SELFTEST_OK"
  "HYBRID: IPC_ADAPTIVE_ADMISSION_SELFTEST_OK"
  "HYBRID: IPC_PREDICTIVE_PRESSURE_SELFTEST_OK"
  "HYBRID: IPC_QOS_ARBITRATION_SELFTEST_OK"
  "HYBRID: IPC_DRR_FAIR_ADMISSION_SELFTEST_OK"
  "HYBRID: IPC_CAPACITY_DISPATCH_SELFTEST_OK"
  "HYBRID: IPC_WORKER_RUNTIME_SELFTEST_OK"
  "HYBRID: IPC_WORKER_POOL_SELFTEST_OK"
  "HYBRID: IPC_DIAGNOSTICS_OK"
  "HYBRID: BOOT_OK"
)
for marker in "${required_markers[@]}"; do
  if ! grep -Fq "$marker" "$log_file"; then
    echo "FAIL: missing expected boot marker: $marker (QEMU exit: $qemu_status)." >&2
    echo "---- QEMU CPU/interrupt trace (last 200 lines) ----"
    tail -n 200 build/qemu-debug.log || true
    exit 1
  fi
done
echo "PASS: Memory Core, GDT/TSS/IST, task lifecycle/reaping, blocking/wakeup, mutex synchronization, timers, semaphores, IPC services, request/response, pending calls, lifecycle shutdown, queue backpressure, delivery receipts, ACK, exponential retry backoff, circuit breakers, adaptive IPC balancing, priorities, congestion control, service health, deadlines, dead-letter queue, bounded IPC event tracing, token-bucket rate limiting, fair per-source receive scheduling, source quotas, priority-aware admission, deadline-aware IPC scheduling, starvation prevention, QoS profiles, message TTL expiration, stale-queue pruning, queue compaction telemetry, capacity metrics, adaptive admission control, predictive queue pressure forecasting, bounded admission budgets, congestion-aware QoS arbitration, deficit round-robin credits, bounded credit accounting, starvation protection, adaptive IPC worker dispatch capacity, cooperative worker tasks, lifecycle, lease recovery, bounded pool scaling and per-worker fault isolation passed in QEMU."
