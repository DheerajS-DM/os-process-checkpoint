import sys
import os
from PyQt6.QtWidgets import QApplication, QMainWindow, QTabWidget, QVBoxLayout, QWidget, QLabel, QStatusBar
from PyQt6.QtCore import QTimer, Qt

from shm_reader import ShmReader
from heatmap_widget import HeatmapWidget
from gantt_widget import GanttWidget
from metrics_widget import MetricsWidget
from chaos_panel import ChaosPanel

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Process Recovery Engine — Telemetry & Control Dashboard")
        self.resize(1400, 920)
        self.setMinimumSize(1200, 800)

        self.shm_reader = ShmReader()

        self.init_ui()
        self.load_styles()

        # 10 Hz Telemetry refresh loop (100 ms)
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update_loop)
        self.timer.start(100)

    def init_ui(self):
        central_widget = QWidget()
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(16, 16, 16, 16)
        main_layout.setSpacing(12)

        # Tab Widget
        self.tabs = QTabWidget()

        # Tab 1: Virtual Memory Heatmap & Metrics HUD
        tab1 = QWidget()
        t1_layout = QVBoxLayout(tab1)
        t1_layout.setContentsMargins(12, 12, 12, 12)
        t1_layout.setSpacing(14)
        
        self.metrics_widget = MetricsWidget()
        self.heatmap_widget = HeatmapWidget()
        t1_layout.addWidget(self.metrics_widget)
        t1_layout.addWidget(self.heatmap_widget, stretch=1)
        self.tabs.addTab(tab1, "📊 Memory Heatmap & HUD")

        # Tab 2: Scheduler Gantt Waterfall
        tab2 = QWidget()
        t2_layout = QVBoxLayout(tab2)
        t2_layout.setContentsMargins(12, 12, 12, 12)
        self.gantt_widget = GanttWidget()
        t2_layout.addWidget(self.gantt_widget)
        self.tabs.addTab(tab2, "⚡ DPC-RR Scheduler Waterfall")

        # Tab 3: Chaos Injection Deck
        tab3 = QWidget()
        t3_layout = QVBoxLayout(tab3)
        t3_layout.setContentsMargins(12, 12, 12, 12)
        self.chaos_panel = ChaosPanel()
        t3_layout.addWidget(self.chaos_panel)
        self.tabs.addTab(tab3, "💥 Chaos & Fault Injection")

        main_layout.addWidget(self.tabs)
        self.setCentralWidget(central_widget)

        # Status bar
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage("Connecting to POSIX shared memory bridge (/dev/shm/wsl_telemetry)...")

    def load_styles(self):
        qss_path = os.path.join(os.path.dirname(__file__), "styles.qss")
        if os.path.exists(qss_path):
            with open(qss_path, "r") as f:
                self.setStyleSheet(f.read())

    def update_loop(self):
        data = self.shm_reader.read_snapshot()
        if data:
            self.status_bar.showMessage(
                f"🟢 Telemetry Live  |  Epoch: {data['epoch']}  |  Uptime: {data['uptime_sec']:.1f}s  |  "
                f"Active Workers: {len(data['workers'])}  |  Guarded RAM: {len(data['workers']) * 256} MB  |  Last RTO: {data['rto_ms']:.2f}ms"
            )
            self.metrics_widget.update_telemetry(data)
            self.heatmap_widget.update_telemetry(data)
            self.gantt_widget.update_telemetry(data)
            self.chaos_panel.update_telemetry(data)
        else:
            self.status_bar.showMessage("🟡 Waiting for C Supervisor daemon to initialize /dev/shm/wsl_telemetry...")

    def closeEvent(self, event):
        self.shm_reader.close()
        event.accept()

def main():
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
