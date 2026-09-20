import mmap
import os
import sys
import ctypes
from typing import Optional, Dict, Any, List

GANTT_RING_SIZE = 512
MAX_WORKERS = 8
HEATMAP_BUCKETS = 4096

class GanttEventStruct(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("timestamp_ns", ctypes.c_int64),
        ("pid", ctypes.c_uint32),
        ("worker_idx", ctypes.c_uint8),
        ("event_type", ctypes.c_uint8),
        ("from_tier", ctypes.c_uint8),
        ("to_tier", ctypes.c_uint8),
        ("details", ctypes.c_char * 24),
    ]

class WorkerTelemetryStruct(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("pid", ctypes.c_uint32),
        ("active", ctypes.c_uint8),
        ("tier", ctypes.c_uint8),
        ("state", ctypes.c_uint8),
        ("pad", ctypes.c_uint8),
        ("p_dyn", ctypes.c_double),
        ("p_base", ctypes.c_double),
        ("cpu_ns", ctypes.c_int64),
        ("wait_ns", ctypes.c_int64),
        ("dirty_pages", ctypes.c_uint32),
        ("total_faults", ctypes.c_uint32),
        ("checkpoint_count", ctypes.c_uint32),
        ("restore_count", ctypes.c_uint32),
        ("last_ckpt_duration_ns", ctypes.c_int64),
        ("last_restore_duration_ns", ctypes.c_int64),
        ("workload_name", ctypes.c_char * 32),
        ("heap_heatmap", ctypes.c_uint8 * HEATMAP_BUCKETS),
    ]

class TelemetryShmStruct(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("epoch", ctypes.c_uint64),
        ("num_workers", ctypes.c_uint32),
        ("pad", ctypes.c_uint32),
        ("supervisor_uptime_ns", ctypes.c_int64),
        ("last_rto_ns", ctypes.c_int64),
        ("last_rpo_ns", ctypes.c_int64),
        ("checkpoint_overhead_pct", ctypes.c_double),
        ("workers", WorkerTelemetryStruct * MAX_WORKERS),
        ("gantt_head", ctypes.c_uint32),
        ("gantt_count", ctypes.c_uint32),
        ("gantt_ring", GanttEventStruct * GANTT_RING_SIZE),
    ]

class ShmReader:
    def __init__(self, distro_name: str = "Ubuntu-26.04"):
        self.distro_name = distro_name
        self.struct_size = ctypes.sizeof(TelemetryShmStruct)
        self.candidate_paths = [
            "/dev/shm/wsl_telemetry",
            rf"\\wsl.localhost\{distro_name}\dev\shm\wsl_telemetry",
            rf"\\wsl$\{distro_name}\dev\shm\wsl_telemetry",
        ]
        self.active_path: Optional[str] = None

    def connect(self) -> bool:
        for p in self.candidate_paths:
            if os.path.exists(p):
                self.active_path = p
                return True
        return False

    def read_snapshot(self) -> Optional[Dict[str, Any]]:
        if not self.connect() or not self.active_path:
            return None
        
        try:
            # Read bytes directly from file / shared memory
            with open(self.active_path, "rb") as f:
                raw_bytes = f.read(self.struct_size)
            
            if len(raw_bytes) < self.struct_size:
                return None
            
            # Lock-free double read using monotonic epoch counter
            struct_obj = TelemetryShmStruct.from_buffer_copy(raw_bytes)
            epoch_1 = struct_obj.epoch
            
            # Extract worker info
            workers = []
            for i in range(struct_obj.num_workers):
                w = struct_obj.workers[i]
                if not w.active:
                    continue
                
                heatmap_bytes = bytes(w.heap_heatmap)
                workers.append({
                    "idx": i,
                    "pid": w.pid,
                    "active": bool(w.active),
                    "tier": w.tier,
                    "state": w.state,
                    "p_dyn": w.p_dyn,
                    "p_base": w.p_base,
                    "cpu_ms": w.cpu_ns / 1_000_000,
                    "wait_ms": w.wait_ns / 1_000_000,
                    "dirty_pages": w.dirty_pages,
                    "total_faults": w.total_faults,
                    "checkpoint_count": w.checkpoint_count,
                    "restore_count": w.restore_count,
                    "last_ckpt_ms": w.last_ckpt_duration_ns / 1_000_000,
                    "workload": w.workload_name.decode("utf-8", errors="ignore").strip("\x00"),
                    "heatmap": heatmap_bytes,
                })
            
            # Extract Gantt events
            events = []
            head = struct_obj.gantt_head
            count = min(struct_obj.gantt_count, GANTT_RING_SIZE)
            for k in range(count):
                idx = (head - count + k) % GANTT_RING_SIZE
                ev = struct_obj.gantt_ring[idx]
                if ev.timestamp_ns > 0:
                    events.append({
                        "timestamp_ns": ev.timestamp_ns,
                        "pid": ev.pid,
                        "worker_idx": ev.worker_idx,
                        "event_type": ev.event_type,
                        "from_tier": ev.from_tier,
                        "to_tier": ev.to_tier,
                        "details": ev.details.decode("utf-8", errors="ignore").strip("\x00"),
                    })

            return {
                "epoch": epoch_1,
                "uptime_sec": struct_obj.supervisor_uptime_ns / 1_000_000_000,
                "rto_ms": struct_obj.last_rto_ns / 1_000_000,
                "rpo_ms": struct_obj.last_rpo_ns / 1_000_000,
                "overhead_pct": struct_obj.checkpoint_overhead_pct,
                "num_workers": struct_obj.num_workers,
                "workers": workers,
                "gantt_events": events,
            }
        except Exception:
            return None

    def close(self):
        self.active_path = None
