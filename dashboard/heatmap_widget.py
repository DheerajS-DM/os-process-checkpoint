from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QComboBox,
    QFrame, QPushButton, QProgressBar
)
from PyQt6.QtCore import Qt
import pyqtgraph as pg
from collections import deque
import numpy as np

class HeatmapWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.selected_worker_idx = 0
        self.dirty_history = deque(maxlen=60)
        self.time_history = deque(maxlen=60)
        self.init_ui()

    def init_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 4, 0, 0)
        layout.setSpacing(14)

        # 1. Top Controls Bar
        ctrl_card = QFrame()
        ctrl_card.setObjectName("ctrl_card")
        ctrl_layout = QHBoxLayout(ctrl_card)
        ctrl_layout.setContentsMargins(18, 12, 18, 12)
        ctrl_layout.setSpacing(16)

        lbl = QLabel("Inspecting Process:")
        lbl.setStyleSheet("font-weight: 700; color: #ffffff; font-size: 13px;")
        ctrl_layout.addWidget(lbl)

        self.worker_selector = QComboBox()
        self.worker_selector.setMinimumWidth(260)
        self.worker_selector.currentIndexChanged.connect(self.on_worker_changed)
        ctrl_layout.addWidget(self.worker_selector)

        self.status_badge = QLabel("RUNNING")
        self.status_badge.setStyleSheet("color: #ffffff; font-weight: 700; font-size: 11px; background-color: #15803d; padding: 5px 12px; border-radius: 4px;")
        ctrl_layout.addWidget(self.status_badge)

        self.narration_lbl = QLabel("Worker is actively mutating memory. Memory automatically flushes to 0 MB upon each disk checkpoint.")
        self.narration_lbl.setStyleSheet("color: #a1a1aa; font-size: 12px;")
        ctrl_layout.addWidget(self.narration_lbl, stretch=1)

        self.btn_flush = QPushButton("🧹 Clear / Flush View")
        self.btn_flush.clicked.connect(self.manual_flush)
        ctrl_layout.addWidget(self.btn_flush)

        layout.addWidget(ctrl_card)

        # 2. Linear Memory Strip View (0 MB to 256 MB Address Map)
        strip_card = QFrame()
        strip_card.setObjectName("strip_card")
        strip_layout = QVBoxLayout(strip_card)
        strip_layout.setContentsMargins(18, 14, 18, 14)
        strip_layout.setSpacing(8)

        strip_header = QHBoxLayout()
        st_title = QLabel("📍 Process Memory Address Space (0 MB ➔ 256 MB)")
        st_title.setStyleSheet("font-weight: 700; color: #ffffff; font-size: 13px; text-transform: uppercase; letter-spacing: 0.5px;")
        strip_header.addWidget(st_title)

        self.strip_info = QLabel("Modified: 0 MB / 256 MB")
        self.strip_info.setStyleSheet("color: #a1a1aa; font-weight: 700; font-size: 12px;")
        strip_header.addWidget(self.strip_info, alignment=Qt.AlignmentFlag.AlignRight)
        strip_layout.addLayout(strip_header)

        # Visual segmented memory strip
        self.memory_strip = QProgressBar()
        self.memory_strip.setRange(0, 256)
        self.memory_strip.setValue(0)
        self.memory_strip.setFixedHeight(28)
        self.memory_strip.setTextVisible(True)
        self.memory_strip.setStyleSheet("""
            QProgressBar {
                border: 1px solid #3f3f46;
                border-radius: 6px;
                text-align: center;
                background-color: #000000;
                color: #ffffff;
                font-weight: 700;
                font-size: 11px;
            }
            QProgressBar::chunk {
                background-color: #ffffff;
                border-radius: 4px;
            }
        """)
        strip_layout.addWidget(self.memory_strip)

        # Address marker labels
        marker_layout = QHBoxLayout()
        marker_layout.addWidget(QLabel("0 MB (Base Heap)"))
        marker_layout.addStretch()
        marker_layout.addWidget(QLabel("64 MB"))
        marker_layout.addStretch()
        marker_layout.addWidget(QLabel("128 MB"))
        marker_layout.addStretch()
        marker_layout.addWidget(QLabel("192 MB"))
        marker_layout.addStretch()
        marker_layout.addWidget(QLabel("256 MB (Heap Limit)"))
        for i in range(marker_layout.count()):
            w = marker_layout.itemAt(i).widget()
            if w:
                w.setStyleSheet("color: #71717a; font-size: 10px; font-family: 'Consolas', monospace;")
        strip_layout.addLayout(marker_layout)

        layout.addWidget(strip_card)

        # 3. Real-Time Memory Activity & Periodic Checkpoint Flush Graph
        chart_card = QFrame()
        chart_card.setObjectName("chart_card")
        chart_layout = QVBoxLayout(chart_card)
        chart_layout.setContentsMargins(18, 14, 18, 14)
        chart_layout.setSpacing(10)

        chart_hdr = QHBoxLayout()
        ch_title = QLabel("📈 Live Memory Modification & Checkpoint Flush Cycle")
        ch_title.setStyleSheet("font-weight: 700; color: #ffffff; font-size: 13px; text-transform: uppercase; letter-spacing: 0.5px;")
        chart_hdr.addWidget(ch_title)

        ch_legend = QLabel("■ Dirty Memory (MB)  |  Periodic Checkpoint drops curve to 0 MB")
        ch_legend.setStyleSheet("color: #a1a1aa; font-size: 11px; font-weight: 600;")
        chart_hdr.addWidget(ch_legend, alignment=Qt.AlignmentFlag.AlignRight)
        chart_layout.addLayout(chart_hdr)

        # PyQtGraph Live Plot
        self.plot_widget = pg.PlotWidget()
        self.plot_widget.setBackground("#000000")
        self.plot_widget.showGrid(x=True, y=True, alpha=0.15)
        self.plot_widget.setLabel("left", "Modified Memory", units="MB")
        self.plot_widget.setLabel("bottom", "Time Slice History", units="ticks")
        self.plot_widget.setYRange(0, 100)
        self.plot_widget.setStyleSheet("border: 1px solid #27272a; border-radius: 6px;")

        self.curve = self.plot_widget.plot(
            pen=pg.mkPen(color="#ffffff", width=2.5),
            brush=pg.mkBrush(color=(255, 255, 255, 30)),
            fillLevel=0
        )
        chart_layout.addWidget(self.plot_widget)

        layout.addWidget(chart_card, stretch=1)

    def on_worker_changed(self, idx):
        self.selected_worker_idx = idx
        self.dirty_history.clear()

    def manual_flush(self):
        self.dirty_history.clear()
        self.memory_strip.setValue(0)
        self.curve.setData([])

    def update_telemetry(self, data):
        if not data or "workers" not in data:
            return

        workers = data["workers"]
        current_names = [f"Worker {w['idx']} ({w['workload']}) - PID {w['pid']}" for w in workers]
        
        if self.worker_selector.count() != len(workers):
            cur = self.worker_selector.currentIndex()
            self.worker_selector.blockSignals(True)
            self.worker_selector.clear()
            self.worker_selector.addItems(current_names)
            if 0 <= cur < len(current_names):
                self.worker_selector.setCurrentIndex(cur)
            self.worker_selector.blockSignals(False)

        target_worker = None
        for w in workers:
            if w["idx"] == self.selected_worker_idx:
                target_worker = w
                break
        if not target_worker and workers:
            target_worker = workers[0]

        if target_worker:
            is_running = (target_worker.get("state", 0) == 1)
            workload_name = target_worker.get("workload", "Computation")
            dirty_pages = target_worker.get("dirty_pages", 0)
            dirty_mb = (dirty_pages * 4096) / (1024 * 1024)

            # Update status badge & narration
            if is_running:
                self.status_badge.setText("🟢 RUNNING")
                self.status_badge.setStyleSheet("color: #ffffff; font-weight: 700; font-size: 11px; background-color: #15803d; padding: 5px 12px; border-radius: 4px;")
                self.narration_lbl.setText(
                    f"Worker {target_worker['idx']} is RUNNING ({workload_name}). Actively accumulating dirty pages before next snapshot."
                )
            else:
                self.status_badge.setText("⏸️ PAUSED")
                self.status_badge.setStyleSheet("color: #ffffff; font-weight: 700; font-size: 11px; background-color: #854d0e; padding: 5px 12px; border-radius: 4px;")
                self.narration_lbl.setText(
                    f"Worker {target_worker['idx']} is PAUSED. Memory is frozen and waiting for next scheduler quantum."
                )

            # Update linear memory strip
            self.memory_strip.setValue(int(min(dirty_mb, 256)))
            self.strip_info.setText(f"Modified: {dirty_mb:.1f} MB / 256 MB ({int((dirty_mb/256.0)*100)}%)")

            # Update real-time live graph
            self.dirty_history.append(dirty_mb)
            self.curve.setData(list(self.dirty_history))
