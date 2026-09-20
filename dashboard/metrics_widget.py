from PyQt6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QLabel, QFrame

class MetricCard(QFrame):
    def __init__(self, title: str, subtitle: str, unit: str = "", parent=None):
        super().__init__(parent)
        self.setObjectName("metric_card")
        self.setMinimumHeight(95)
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(14, 12, 14, 12)
        layout.setSpacing(4)

        self.title_lbl = QLabel(title)
        self.title_lbl.setStyleSheet("font-size: 11px; font-weight: 700; color: #a1a1aa; text-transform: uppercase; letter-spacing: 0.5px;")
        layout.addWidget(self.title_lbl)

        val_layout = QHBoxLayout()
        val_layout.setContentsMargins(0, 0, 0, 0)
        val_layout.setSpacing(6)
        
        self.val_lbl = QLabel("0.0")
        self.val_lbl.setStyleSheet("color: #ffffff; font-size: 24px; font-weight: 800; font-family: 'Consolas', monospace;")
        val_layout.addWidget(self.val_lbl)

        if unit:
            self.unit_lbl = QLabel(unit)
            self.unit_lbl.setStyleSheet("color: #71717a; font-size: 12px; font-weight: 700; padding-top: 6px;")
            val_layout.addWidget(self.unit_lbl)
        val_layout.addStretch()
        layout.addLayout(val_layout)

        self.sub_lbl = QLabel(subtitle)
        self.sub_lbl.setStyleSheet("color: #71717a; font-size: 11px;")
        layout.addWidget(self.sub_lbl)

    def set_value(self, val: float, format_str: str = "{:.1f}"):
        self.val_lbl.setText(format_str.format(val))

class MetricsWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.init_ui()

    def init_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        grid = QGridLayout()
        grid.setSpacing(12)

        self.card_rto = MetricCard("⚡ Crash Recovery Speed", "Time to resurrect killed process", "ms")
        self.card_rpo = MetricCard("💾 Snapshot Freshness", "Age of newest checkpoint", "sec")
        self.card_overhead = MetricCard("⏱️ Checkpoint Overhead", "CPU tracking impact", "%")
        self.card_memory = MetricCard("🧠 Guarded RAM", "Total protected memory", "MB")

        grid.addWidget(self.card_rto, 0, 0)
        grid.addWidget(self.card_rpo, 0, 1)
        grid.addWidget(self.card_overhead, 0, 2)
        grid.addWidget(self.card_memory, 0, 3)
        layout.addLayout(grid)

    def update_telemetry(self, data):
        if not data:
            return

        rto_ms = data.get("rto_ms", 0.0)
        rpo_sec = data.get("rpo_ms", 0.0) / 1000.0
        overhead_pct = data.get("overhead_pct", 0.0)
        num_workers = data.get("num_workers", 0)

        self.card_rto.set_value(rto_ms, "{:.2f}")
        self.card_rpo.set_value(rpo_sec, "{:.1f}")
        self.card_overhead.set_value(overhead_pct, "{:.2f}")
        self.card_memory.set_value(num_workers * 256.0, "{:.0f}")

