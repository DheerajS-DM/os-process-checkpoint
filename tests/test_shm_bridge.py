import os
import sys
import ctypes

# Add dashboard to python path to import shm_reader
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "dashboard")))

from shm_reader import TelemetryShmStruct, WorkerTelemetryStruct, GanttEventStruct, GANTT_RING_SIZE, MAX_WORKERS, HEATMAP_BUCKETS

def test_struct_sizes():
    print("=== Testing CTypes Shared Memory Struct Layout ===")
    
    gantt_size = ctypes.sizeof(GanttEventStruct)
    worker_size = ctypes.sizeof(WorkerTelemetryStruct)
    shm_size = ctypes.sizeof(TelemetryShmStruct)
    
    print(f"GanttEventStruct size:       {gantt_size} bytes")
    print(f"WorkerTelemetryStruct size:  {worker_size} bytes")
    print(f"TelemetryShmStruct size:     {shm_size} bytes ({shm_size / 1024:.2f} KB)")
    
    # Must be packed and fit within 2MB budget
    assert shm_size < 2 * 1024 * 1024, "SHM struct exceeded 2MB budget!"
    assert gantt_size == 40, f"Unexpected GanttEventStruct size {gantt_size}"
    
    # Test instantiation & serialization
    buf = bytearray(shm_size)
    shm_obj = TelemetryShmStruct.from_buffer(buf)
    shm_obj.epoch = 42
    shm_obj.num_workers = 2
    
    assert shm_obj.epoch == 42
    assert shm_obj.num_workers == 2
    
    print("CTypes Shared Memory Struct Layout: ALL CHECKS PASSED ✅")

if __name__ == "__main__":
    test_struct_sizes()
