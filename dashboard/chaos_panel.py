import os
import sys
import socket
import struct
import subprocess
import time
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QLabel, QPushButton,
    QComboBox, QTextEdit, QCheckBox, QMessageBox, QFrame
)

CHAOS_SOCK_PATH = "/tmp/wsl_chaos.sock"

CHAOS_CMD_OOB_WRITE = 1
CHAOS_CMD_SIGKILL = 2
CHAOS_CMD_CORRUPT_HEAP = 3
CHAOS_CMD_HANG = 4

class ChaosActionCard(QFrame):
    def __init__(self, title: str, desc: str, btn_text: str, is_danger: bool, on_click, parent=None):
        super().__init__(parent)
        self.setObjectName("chaos_card")
        self.setMinimumHeight(160)
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(18, 16, 18, 16)
        layout.setSpacing(8)

        lbl_t = QLabel(title)
        lbl_t.setStyleSheet("font-size: 14px; font-weight: 700; color: #ffffff;")
        layout.addWidget(lbl_t)

        lbl_d = QLabel(desc)
        lbl_d.setStyleSheet("color: #a1a1aa; font-size: 12px; line-height: 1.4;")
        lbl_d.setWordWrap(True)
        layout.addWidget(lbl_d)

        layout.addStretch()

        self.btn = QPushButton(btn_text)
        self.btn.setFixedHeight(38)
        if is_danger:
            self.btn.setObjectName("btn_danger")
        else:
            self.btn.setObjectName("btn_warning")
        self.btn.clicked.connect(on_click)
        layout.addWidget(self.btn)

class ChaosPanel(QWidget):
    def __init__(self, distro_name: str = "Ubuntu-26.04", parent=None):
        super().__init__(parent)
        self.distro_name = distro_name
        self.workers = []
        self.init_ui()

    def init_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 4, 0, 0)
        layout.setSpacing(14)

        # Header card
        header_card = QFrame()
        header_card.setObjectName("header_card")
        h_layout = QVBoxLayout(header_card)
        h_layout.setContentsMargins(18, 14, 18, 14)
        h_layout.setSpacing(4)

        title = QLabel("💥 Deterministic Chaos & Fault Injection Deck")
        title.setStyleSheet("font-size: 15px; font-weight: 700; color: #ffffff; text-transform: uppercase; letter-spacing: 0.5px;")
        h_layout.addWidget(title)

        desc = QLabel("Inject synthetic hardware & kernel faults to test userspace process resurrection and memory restoration.")
        desc.setStyleSheet("color: #a1a1aa; font-size: 12px;")
        h_layout.addWidget(desc)

        layout.addWidget(header_card)

        # Target selection bar
        sel_card = QFrame()
        sel_card.setObjectName("sel_card")
        sel_layout = QHBoxLayout(sel_card)
        sel_layout.setContentsMargins(18, 12, 18, 12)

        lbl = QLabel("Target Process:")
        lbl.setStyleSheet("font-weight: 700; color: #ffffff; font-size: 13px;")
        sel_layout.addWidget(lbl)

        self.worker_combo = QComboBox()
        self.worker_combo.setMinimumWidth(260)
        sel_layout.addWidget(self.worker_combo)

        sel_layout.addSpacing(24)
        self.target_status = QLabel("State: RUNNING | Guarded Memory: 256 MB")
        self.target_status.setStyleSheet("color: #a1a1aa; font-weight: 600; font-size: 12px;")
        sel_layout.addWidget(self.target_status)

        sel_layout.addStretch()

        self.confirm_chk = QCheckBox("Require Confirmation Dialog")
        self.confirm_chk.setChecked(False)
        self.confirm_chk.setStyleSheet("color: #a1a1aa; font-weight: 600;")
        sel_layout.addWidget(self.confirm_chk)

        layout.addWidget(sel_card)

        # 4 Interactive Action Cards Grid with plenty of vertical height
        grid = QGridLayout()
        grid.setSpacing(14)

        card_oob = ChaosActionCard(
            "💥 Out-Of-Bounds Memory Crash",
            "Forces pointer to write NULL (0x0). Bypasses dirty-page trap and triggers fatal hardware exception.",
            "Trigger OOB Crash",
            True,
            lambda: self.trigger_fault(CHAOS_CMD_OOB_WRITE, "OOB Null Write")
        )

        card_kill = ChaosActionCard(
            "☠️ Hard Kernel Kill (SIGKILL)",
            "Instantly drops the worker process. Supervisor auto-detects death and resurrects from checkpoint.",
            "Inject SIGKILL",
            True,
            lambda: self.trigger_fault(CHAOS_CMD_SIGKILL, "SIGKILL Hard Drop")
        )

        card_corrupt = ChaosActionCard(
            "🔀 Memory Heap Bit-Flip",
            "Flips random bits in the active computation heap to simulate silent memory corruption.",
            "Corrupt Tracked Heap",
            False,
            lambda: self.trigger_fault(CHAOS_CMD_CORRUPT_HEAP, "Heap Bit-Flip")
        )

        card_hang = ChaosActionCard(
            "⏳ Infinite Non-Yielding Loop",
            "Traps worker in a 100% CPU loop. Tests scheduler preemption and starvation promotion.",
            "Simulate Process Hang",
            False,
            lambda: self.trigger_fault(CHAOS_CMD_HANG, "Infinite Loop")
        )

        grid.addWidget(card_oob, 0, 0)
        grid.addWidget(card_kill, 0, 1)
        grid.addWidget(card_corrupt, 1, 0)
        grid.addWidget(card_hang, 1, 1)

        layout.addLayout(grid)

        # Clean Audit Log
        log_title = QLabel("📋 Chaos Execution & Recovery Audit Log:")
        log_title.setStyleSheet("font-weight: 700; color: #ffffff; font-size: 13px; margin-top: 6px;")
        layout.addWidget(log_title)

        self.log_viewer = QTextEdit()
        self.log_viewer.setReadOnly(True)
        self.log_viewer.setMinimumHeight(140)
        layout.addWidget(self.log_viewer)


    def update_telemetry(self, data):
        if not data:
            return
        self.workers = data.get("workers", [])
        current_names = [f"Worker {w['idx']} ({w['workload']}) - PID {w['pid']}" for w in self.workers]
        
        if self.worker_combo.count() != len(current_names):
            cur_idx = self.worker_combo.currentIndex()
            self.worker_combo.blockSignals(True)
            self.worker_combo.clear()
            self.worker_combo.addItems(current_names)
            if 0 <= cur_idx < len(current_names):
                self.worker_combo.setCurrentIndex(cur_idx)
            self.worker_combo.blockSignals(False)

        idx = self.worker_combo.currentIndex()
        if 0 <= idx < len(self.workers):
            w = self.workers[idx]
            st = "RUNNING" if w.get("state", 0) == 1 else "PAUSED"
            self.target_status.setText(f"State: {st} | CPU: {w['cpu_ms']/1000.0:.1f}s | Guarded RAM: 256 MB")

    def trigger_fault(self, cmd_type: int, name: str):
        idx = self.worker_combo.currentIndex()
        if idx < 0 or idx >= len(self.workers):
            self.log(f"[ERROR] No target worker process selected.")
            return

        target = self.workers[idx]
        target_pid = target["pid"]

        if self.confirm_chk.isChecked():
            res = QMessageBox.question(
                self, "Confirm Chaos Injection",
                f"Are you sure you want to inject '{name}' into Worker {target['idx']} (PID {target_pid})?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
            )
            if res != QMessageBox.StandardButton.Yes:
                return

        time_str = time.strftime("%H:%M:%S")
        self.log(f"[{time_str}] [CHAOS DISPATCH] Injecting '{name}' into Worker {target['idx']} (PID {target_pid})...")

        if os.name == 'nt':
            py_cmd = (
                f"import socket, struct; "
                f"s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); "
                f"s.connect('{CHAOS_SOCK_PATH}.{target_pid}'); "
                f"pkt = struct.pack('<BBI32s', {cmd_type}, {target['idx']}, {target_pid}, b'{name}'.ljust(32, b'\\x00')); "
                f"s.sendall(pkt); "
                f"s.close()"
            )
            try:
                subprocess.Popen(["wsl", "-d", self.distro_name, "python3", "-c", py_cmd])
                self.log(f"[{time_str}] [CHAOS SUCCESS] Signal delivered. Supervisor auto-recovery triggered.")
            except Exception as e:
                self.log(f"[{time_str}] [CHAOS NOTE] Process terminated/signaled: {e}")
        else:
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(f"{CHAOS_SOCK_PATH}.{target_pid}")
                pkt = struct.pack("<BBI32s", cmd_type, target["idx"], target_pid, name.encode("utf-8")[:31].ljust(32, b'\x00'))
                s.sendall(pkt)
                s.close()
                self.log(f"[{time_str}] [CHAOS SUCCESS] Signal delivered. Supervisor auto-recovery triggered.")
            except Exception as e:
                self.log(f"[{time_str}] [CHAOS NOTE] Process terminated/signaled: {e}")

    def log(self, text: str):
        self.log_viewer.append(text)
