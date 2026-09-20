from PyQt6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel, QTableWidget, QTableWidgetItem, QHeaderView
import pyqtgraph as pg
import time

EVENT_NAMES = {
    0: "NONE",
    1: "SPAWN",
    2: "SCHEDULE",
    3: "PREEMPT",
    4: "PROMOTE",
    5: "DEMOTE",
    6: "CKPT_START",
    7: "CKPT_DONE",
    8: "CRASH",
    9: "RESTORE_START",
    10: "RESTORE_DONE"
}

TIER_COLORS = {
    0: "#ffffff",
    1: "#facc15",
    2: "#f87171",
}

STATE_NAMES = {
    0: ("IDLE", "#71717a"),
    1: ("RUNNING", "#ffffff"),
    2: ("PAUSED", "#a1a1aa"),
    3: ("CHECKPOINT", "#c084fc"),
    4: ("DEAD", "#ef4444"),
}

class GanttWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.init_ui()

    def init_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 4, 0, 0)
        layout.setSpacing(14)

        # Header info
        hdr_layout = QHBoxLayout()
        lbl = QLabel("DPC-RR Scheduling Queue & Priority Circulation")
        lbl.setStyleSheet("font-weight: 700; color: #ffffff; font-size: 14px; text-transform: uppercase; letter-spacing: 0.5px;")
        hdr_layout.addWidget(lbl)
        hdr_layout.addStretch()

        q0_lbl = QLabel("■ Tier 0 (100ms)")
        q0_lbl.setStyleSheet("color: #ffffff; font-weight: 700;")
        q1_lbl = QLabel("■ Tier 1 (250ms)")
        q1_lbl.setStyleSheet("color: #facc15; font-weight: 700;")
        q2_lbl = QLabel("■ Tier 2 (600ms)")
        q2_lbl.setStyleSheet("color: #f87171; font-weight: 700;")

        hdr_layout.addWidget(q0_lbl)
        hdr_layout.addWidget(q1_lbl)
        hdr_layout.addWidget(q2_lbl)
        layout.addLayout(hdr_layout)

        # Active Tasks Table
        self.task_table = QTableWidget()
        self.task_table.setColumnCount(9)
        self.task_table.setHorizontalHeaderLabels([
            "Worker", "PID", "State", "Workload", "Tier", "P_dyn", "P_base", "CPU Time", "Wait Time"
        ])
        self.task_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        self.task_table.setMinimumHeight(180)
        layout.addWidget(self.task_table)

        # Event stream log
        log_title = QLabel("📋 High-Signal Lifecycle, Crash & Recovery Log:")
        log_title.setStyleSheet("font-weight: 700; color: #ffffff; font-size: 13px; margin-top: 6px;")
        layout.addWidget(log_title)

        self.event_table = QTableWidget()
        self.event_table.setColumnCount(5)
        self.event_table.setHorizontalHeaderLabels([
            "Timestamp", "PID", "Worker", "Event Type", "Event Summary"
        ])
        self.event_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        layout.addWidget(self.event_table)

    def update_telemetry(self, data):
        if not data:
            return

        # 1. Update Tasks Table
        workers = data.get("workers", [])
        self.task_table.setRowCount(len(workers))
        for r, w in enumerate(workers):
            tier_str = f"Tier {w['tier']}"
            p_dyn_str = f"{w['p_dyn']:.2f}"
            p_base_str = f"{w['p_base']:.2f}"
            state_info = STATE_NAMES.get(w.get("state", 0), ("UNKNOWN", "#a1a1aa"))
            
            self.task_table.setItem(r, 0, QTableWidgetItem(f"Worker {w['idx']}"))
            self.task_table.setItem(r, 1, QTableWidgetItem(str(w['pid'])))
            
            state_item = QTableWidgetItem(state_info[0])
            state_item.setForeground(pg.mkColor(state_info[1]))
            self.task_table.setItem(r, 2, state_item)
            
            self.task_table.setItem(r, 3, QTableWidgetItem(w['workload']))
            
            tier_item = QTableWidgetItem(tier_str)
            tier_color = TIER_COLORS.get(w['tier'], "#a1a1aa")
            tier_item.setForeground(pg.mkColor(tier_color))
            self.task_table.setItem(r, 4, tier_item)
            
            self.task_table.setItem(r, 5, QTableWidgetItem(p_dyn_str))
            self.task_table.setItem(r, 6, QTableWidgetItem(p_base_str))
            self.task_table.setItem(r, 7, QTableWidgetItem(f"{w['cpu_ms'] / 1000.0:.2f} s"))
            self.task_table.setItem(r, 8, QTableWidgetItem(f"{w['wait_ms'] / 1000.0:.2f} s"))

        # 2. Filter & Deduplicate High-Signal Events
        raw_events = data.get("gantt_events", [])
        filtered_events = []
        last_sig = None
        
        for ev in raw_events:
            ev_type = ev["event_type"]
            # Exclude routine high-frequency schedule/demote spam from cluttering the table
            sig = (ev_type, ev["pid"], ev["worker_idx"], ev.get("details", ""))
            if sig != last_sig:
                filtered_events.append(ev)
                last_sig = sig
        
        display_events = filtered_events[-18:]
        display_events.reverse()
        
        self.event_table.setRowCount(len(display_events))
        for r, ev in enumerate(display_events):
            ev_name = EVENT_NAMES.get(ev["event_type"], f"EV_{ev['event_type']}")
            ts_sec = ev["timestamp_ns"] / 1_000_000_000
            time_str = time.strftime("%H:%M:%S", time.localtime(ts_sec)) + f".{(ev['timestamp_ns'] // 1_000_000) % 1000:03d}"
            
            self.event_table.setItem(r, 0, QTableWidgetItem(time_str))
            self.event_table.setItem(r, 1, QTableWidgetItem(str(ev["pid"]) if ev["pid"] > 0 else "-"))
            self.event_table.setItem(r, 2, QTableWidgetItem(f"Worker {ev['worker_idx']}"))
            
            ev_item = QTableWidgetItem(ev_name)
            if "CRASH" in ev_name:
                ev_item.setForeground(pg.mkColor("#ef4444"))
            elif "RESTORE" in ev_name:
                ev_item.setForeground(pg.mkColor("#22c55e"))
            elif "CKPT" in ev_name:
                ev_item.setForeground(pg.mkColor("#c084fc"))
            elif "PROMOTE" in ev_name:
                ev_item.setForeground(pg.mkColor("#38bdf8"))
            else:
                ev_item.setForeground(pg.mkColor("#ffffff"))
            self.event_table.setItem(r, 3, ev_item)
            
            self.event_table.setItem(r, 4, QTableWidgetItem(ev.get("details", "")))
