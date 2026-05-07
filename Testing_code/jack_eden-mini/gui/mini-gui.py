"""
mini-gui.py - Serial port GUI for iBeacon indoor positioning system.

Provides a PyQt5-based interface for connecting to a serial device,
visualising raw and Kalman-filtered position estimates on a 2-D grid,
managing iBeacon nodes (add / delete / view), and monitoring raw BLE
advertisements.
"""

# Fix dropdown boxes not staying open on some Linux desktop environments.
import os

os.environ["QT_QPA_PLATFORM"] = "xcb"

import sys
import json
import requests
import math
from datetime import datetime
from typing import Optional
from PyQt5.QtCore import QThread, pyqtSignal, Qt, QObject, pyqtSlot, QTimer
from PyQt5.QtGui import QPainter, QColor, QPen, QFont
from PyQt5.QtWidgets import (
    QApplication,
    QWidget,
    QVBoxLayout,
    QMainWindow,
    QMessageBox,
    QTabWidget,
    QFrame,
    QGridLayout,
    QPushButton,
    QComboBox,
    QLabel,
    QTextEdit,
    QGroupBox,
    QHBoxLayout,
    QLineEdit,
    QCheckBox,
    QTableWidget,
    QTableWidgetItem,
    QSizePolicy,
)
import serial
from serial.tools import list_ports
import serial.tools.list_ports


class GridWidget(QWidget):
    """
    Custom widget that renders a 2-D grid and overlays position dots.

    The grid represents a physical space where:
      - The horizontal axis maps to Y (0-8 500 mm, 85 columns of 100 mm each).
      - The vertical axis maps to X (0-3 400 mm, 34 rows of 100 mm each).

    Two dots are drawn:
      - Red - raw (unfiltered) position estimate.
      - Green - Kalman-filtered position estimate.
    """

    MARGIN_LEFT = 30  # Pixels reserved for X-axis labels.
    MARGIN_BOTTOM = 20  # Pixels reserved for Y-axis labels.

    def __init__(self):
        super().__init__()

        self.grid_cols = 85  # Number of columns  (Y: 0 → 8 500 mm).
        self.grid_rows = 34  # Number of rows     (X: 0 → 3 400 mm).
        self.cell_size = 15  # Pixels per cell; recalculated on resize.

        # Current grid cell indices for each dot.
        self.raw_row = -1
        self.raw_col = -1
        self.kalman_row = -1
        self.kalman_col = -1

    def set_kalman_position(self, x: float, y: float) -> None:
        """
        Update the Kalman-filtered dot position and trigger a repaint.

        Parameters
        ----------
        x : float
            X coordinate in millimetres.
        y : float
            Y coordinate in millimetres.
        """
        self.kalman_col = round(y / 100)
        self.kalman_row = round(x / 100)
        self.update()

    def set_raw_position(self, x: float, y: float) -> None:
        """
        Update the raw (unfiltered) dot position and trigger a repaint.

        Parameters
        ----------
        x : float
            X coordinate in millimetres.
        y : float
            Y coordinate in millimetres.
        """
        self.raw_col = round(y / 100)  # Y maps to the horizontal axis.
        self.raw_row = round(x / 100)  # X maps to the vertical axis.
        self.update()

    def paintEvent(self, event):
        """
        Render the grid, axis labels, and position dots.

        Called automatically by Qt whenever the widget needs to be redrawn.
        """
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        # --- Grid lines ---
        pen = QPen(QColor(200, 200, 200))
        pen.setWidth(1)
        painter.setPen(pen)

        for i in range(self.grid_cols + 1):
            x = self.MARGIN_LEFT + i * self.cell_size
            painter.drawLine(x, 0, x, self.grid_rows * self.cell_size)

        for i in range(self.grid_rows + 1):
            y = i * self.cell_size
            painter.drawLine(
                self.MARGIN_LEFT,
                y,
                self.MARGIN_LEFT + self.grid_cols * self.cell_size,
                y,
            )

        # --- X-axis labels (left side, every 500 mm) ---
        label_font = QFont("Courier", 7)
        painter.setFont(label_font)
        painter.setPen(QPen(QColor(80, 80, 80)))

        for i in range(self.grid_rows + 1):
            mm = i * 100
            if mm % 500 == 0:
                y = i * self.cell_size
                painter.drawText(2, y + 5, f"{mm}")

        # --- Y-axis labels (bottom edge, every 500 mm) ---
        for i in range(self.grid_cols + 1):
            mm = i * 100
            if mm % 500 == 0:
                x = self.MARGIN_LEFT + i * self.cell_size
                painter.drawText(x - 10, self.grid_rows * self.cell_size + 15, f"{mm}")

        # --- Axis titles ---
        title_font = QFont("Courier", 8, QFont.Bold)
        painter.setFont(title_font)
        painter.setPen(QPen(QColor(40, 40, 40)))

        # "Y (mm)" centred below the grid.
        painter.drawText(
            self.MARGIN_LEFT + (self.grid_cols * self.cell_size) // 2 - 15,
            self.grid_rows * self.cell_size + self.MARGIN_BOTTOM - 2,
            "Y (mm)",
        )

        # "X (mm)" rotated 90°, centred to the left of the grid.
        painter.save()
        painter.translate(10, self.grid_rows * self.cell_size // 2 + 15)
        painter.rotate(-90)
        painter.drawText(0, 0, "X (mm)")
        painter.restore()

        painter.setPen(Qt.NoPen)

        # --- Raw position dot (red) ---
        painter.setBrush(QColor(220, 50, 50))
        self._draw_dot(self.raw_row, self.raw_col, painter)

        # --- Kalman position dot (green) ---
        painter.setBrush(QColor(50, 220, 50))
        self._draw_dot(self.kalman_row, self.kalman_col, painter)

    def _draw_dot(self, row, col, painter) -> None:
        """
        Draws a circular dot on the grid at the specified row and column.
        """
        padding = 2
        if 0 <= col < self.grid_cols and 0 <= row < self.grid_rows:
            painter.drawEllipse(
                self.MARGIN_LEFT + col * self.cell_size + padding,
                row * self.cell_size + padding,
                self.cell_size - 2 * padding,
                self.cell_size - 2 * padding,
            )

    def resizeEvent(self, event):
        """
        Recalculate the cell size to fill the available area on resize.

        Ensures the grid scales proportionally when the window is resized,
        while keeping cells square and at least 1 pixel wide.
        """
        available_w = self.width() - self.MARGIN_LEFT
        available_h = self.height() - self.MARGIN_BOTTOM
        self.cell_size = max(
            1, min(available_w // self.grid_cols, available_h // self.grid_rows)
        )
        self.update()
        super().resizeEvent(event)


class SerialReader(QThread):
    """
    Background thread that reads JSON-formatted lines from a serial port.

    Each valid JSON line is emitted via "data_received_signal". Lines that
    cannot be decoded as JSON are forwarded to the caller's logger.  A
    "port_error_signal" is emitted if the port is unexpectedly disconnected.

    Signals
    -------
    data_received_signal : dict
        Emitted for each successfully parsed JSON object.
    data_parse_error_signal : str
        Emitted for and non-json inputs. (e.g. debug prints from the firmware).
    port_error_signal :
        Emitted when a "SerialException" is encountered during reading.
    """

    data_received_signal = pyqtSignal(dict)
    data_parse_error_signal = pyqtSignal(str)
    port_error_signal = pyqtSignal()

    def __init__(self, serial_port: serial.Serial) -> None:
        """
        Initialise the reader thread.

        Parameters
        ----------
        serial_port : serial.Serial
            An open serial port to read from.
        """
        super().__init__()
        self._running = True
        self._ser = serial_port

    def run(self) -> None:
        """
        Main thread loop.

        Reads lines from the serial port and emits data_received_signal
        for each successfully parsed JSON object. Runs until stop() is
        called or the port closes.
        """
        while self._running and self._ser and self._ser.is_open:
            try:
                line: str = self._ser.readline().decode("utf-8").strip()
                if line:
                    try:
                        data = json.loads(line)
                        if isinstance(data, dict):
                            self.data_received_signal.emit(data)
                        else:
                            self.data_parse_error_signal.emit(
                                f"Unexpected JSON type: {line}"
                            )
                    except json.JSONDecodeError:
                        # Non-JSON output from firmware; pass to logger.
                        self.data_parse_error_signal.emit(line)
            except UnicodeDecodeError as e:
                print(f"Couldn't decode data: {e}")
            except serial.SerialException as e:
                print(f"Serial port disconnected: {e}")
                self.port_error_signal.emit()

            self.msleep(10)

    def stop(self) -> None:
        """Signal the thread to stop and block until it exits."""
        self._running = False
        self.wait()


class BeaconViewWidget(QWidget):
    """
    Widget for querying and displaying stored iBeacon nodes.

    Owns the view-request controls and the results table.  Implements a
    simple three-packet state machine to reassemble the start / node
    / end wire protocol sent by the base chip.

    Wire protocol (JSON)::

        {"cmd": "view", "sub": "start"}
        {"cmd": "view", "sub": "node",
         "opts": [name, mac, major, minor, x, y, cali, left, right]}
        {"cmd": "view", "sub": "end"}
    """

    def __init__(self, on_send_cmd) -> None:
        """
        Initialise the widget.

        Parameters
        ----------
        on_send_cmd : callable
            Callback with signature (cmd: str, sub: str, opts: list)
            used to send commands over the serial link.
        """
        super().__init__()
        self._on_send_cmd = on_send_cmd
        self._buffer: list = []  # Accumulates node rows between start/end.
        self._init_ui()

    def _init_ui(self) -> None:
        """Build and arrange child widgets."""
        layout: QVBoxLayout = QVBoxLayout(self)

        # --- Query controls ---
        input_layout: QHBoxLayout = QHBoxLayout()

        self._view_all_check = QCheckBox("All")
        self._view_name = QLineEdit()
        self._view_name.setPlaceholderText("Node name...")

        # Disable the name field when "All" is checked.
        self._view_all_check.stateChanged.connect(
            lambda state: self._view_name.setDisabled(state == Qt.Checked)
        )

        input_layout.addWidget(self._view_all_check)
        input_layout.addWidget(QLabel("Name:"))
        input_layout.addWidget(self._view_name)

        view_btn = QPushButton("View")
        view_btn.setStyleSheet(
            "background-color: #2196F3; color: white; font-weight: bold; padding: 6px;"
        )
        view_btn.clicked.connect(self._request_view)
        input_layout.addWidget(view_btn)

        layout.addLayout(input_layout)

        # --- Results table ---
        self._table = QTableWidget(0, 8)
        self._table.setHorizontalHeaderLabels(
            ["Name", "MAC", "Major", "Minor", "X", "Y", "Cali", "Left / Right"]
        )
        self._table.setEditTriggers(QTableWidget.NoEditTriggers)
        layout.addWidget(self._table)

    def _request_view(self) -> None:
        """Send the appropriate view command based on the current UI state."""
        if self._view_all_check.isChecked():
            self._on_send_cmd("iBeacon", "view", ["-a"])
        else:
            name = self._view_name.text()
            if not name:
                QMessageBox.warning(self, "Input Error", "Enter a name or check All.")
                return
            self._on_send_cmd("iBeacon", "view", [name])

    def handle_view_packet(self, sub: str, opts: list) -> None:
        """
        Process a single view packet received from the base chip.

        Should be called by the controller whenever data["cmd"] == "view".

        Parameters
        ----------
        sub : str
            Packet sub-type: "start", "node", or "end".
        opts : list
            Packet payload; only used when *sub* is "node".
        """
        if sub == "start":
            self._buffer = []
        elif sub == "node":
            self._buffer.append(opts)
        elif sub == "end":
            self._populate_table(self._buffer)

    def _populate_table(self, nodes: list) -> None:
        """
        Rebuild the result table from a complete list of node records.

        Parameters
        ----------
        nodes : list
            Each element is a list of node fields in wire order:
            [name, mac, major, minor, x, y, cali, left, right].
        """
        self._table.setRowCount(0)
        for node in nodes:
            row = self._table.rowCount()
            self._table.insertRow(row)

            # Columns 0-6: name through calibration value.
            for i in range(7):
                self._table.setItem(row, i, QTableWidgetItem(str(node[i])))

            # Column 7: neighbour beacons displayed as "left / right".
            self._table.setItem(row, 7, QTableWidgetItem(f"{node[7]} / {node[8]}"))

        self._table.resizeColumnsToContents()


class DistanceWidget(QWidget):
    """
    Represents a widget displaying live distance estimates for various beacons.
    """

    BEACONS = [
        "4011-A",
        "4011-B",
        "4011-C",
        "4011-D",
        "4011-E",
        "4011-F",
        "4011-G",
        "4011-H",
        "4011-I",
        "4011-J",
        "4011-K",
        "4011-L",
        "4011-M",
    ]
    CLOSE_M = 2000
    MEDIUM_M = 5000

    def __init__(self) -> None:
        super().__init__()
        self._cards: dict = {}
        self._init_ui()

    def _make_card(self, name: str):
        """
        Constructs a styled card layout to display information.
        """
        frame = QFrame()
        frame.setFrameShape(QFrame.StyledPanel)
        frame.setStyleSheet("QFrame { border: 1px solid #cccccc; border-radius: 4px; }")
        layout = QVBoxLayout(frame)
        layout.setContentsMargins(8, 6, 8, 6)
        layout.setSpacing(4)

        name_lbl = QLabel(name)
        name_lbl.setFont(QFont("Courier", 9, QFont.Bold))
        layout.addWidget(name_lbl)

        dist_lbl = QLabel("-- m")
        dist_lbl.setFont(QFont("Courier", 14, QFont.Bold))
        dist_lbl.setStyleSheet("border: none;")
        dist_lbl.setAlignment(Qt.AlignCenter)
        layout.addWidget(dist_lbl)

        bar = QLabel()
        bar.setFixedHeight(6)
        bar.setStyleSheet("background: #cccccc; border-radius: 3px;")
        layout.addWidget(bar)

        return frame, dist_lbl, bar

    def _init_ui(self) -> None:
        """
        Initialises and arranges the user interface elements for displaying live distance estimates.
        """
        outer = QVBoxLayout(self)
        outer.setContentsMargins(12, 12, 12, 12)

        title = QLabel("Live Distance Estimates")
        title.setFont(QFont("Courier", 10, QFont.Bold))
        outer.addWidget(title)

        grid = QGridLayout()
        grid.setSpacing(8)
        for idx, name in enumerate(self.BEACONS):
            row, col = divmod(idx, 3)
            frame, dist_lbl, bar = self._make_card(name)
            self._cards[name] = (frame, dist_lbl, bar)
            grid.addWidget(frame, row, col)
        grid.setRowStretch(len(self.BEACONS) // 3 + 1, 1)

        outer.addLayout(grid)
        outer.addStretch()

    def update_distance(self, name: str, dist: float) -> None:
        """
        Updates the distance label and progress bar based on the provided distance
        value. Changes the progress bar's colour depending on the distance threshold.

        Parameters:
        name: str
            The identifier of the item to update.
        dist: float
            The distance value to update, in millimeters.

        Returns:
        None
        """
        if name not in self._cards:
            return
        _, dist_lbl, bar = self._cards[name]
        dist_lbl.setText(f"{dist:,.0f} mm")
        if dist <= self.CLOSE_M:
            colour = "#4caf50"
        elif dist <= self.MEDIUM_M:
            colour = "#ff9800"
        else:
            colour = "#f44336"
        bar.setStyleSheet(f"background: {colour}; border-radius: 3px;")


class WebWorker(QObject):
    """
    Handles communication with a web server in a worker thread.

    The WebWorker class facilitates the uploading of data to a web server
    and provides a mechanism to emit signals upon successful completion.
    It is designed to work with PyQt's signal-slot mechanism, allowing
    thread-safe operations and event handling in GUI applications.
    """

    data_posted = pyqtSignal(object)

    @pyqtSlot(list)
    def upload_data(self, data: list):
        response = send_to_webserver(data)
        if response is not None:
            self.data_posted.emit(response)


class WebThread(QThread):
    """
    Class to manage a worker process in a separate thread.

    The WebThread class inherits from QThread and is specifically designed to
    run a WebWorker instance in its own thread. The WebWorker is moved to the
    context of this thread to ensure proper thread-safe execution. Once started,
    this thread executes its own event loop, allowing the worker to process tasks
    independently without blocking the main application thread.
    """

    def __init__(self) -> None:
        super().__init__()
        self.worker = WebWorker()
        self.worker.moveToThread(self)  # Worker now lives in this thread

    def run(self) -> None:
        self.exec()


class Controller(QMainWindow):
    """
    Main application window and central controller.

    Manages the serial connection lifecycle, routes incoming serial data to
    the appropriate UI components, and owns all top-level widgets.
    """

    web_data_signal = pyqtSignal(list)

    def __init__(self) -> None:
        super().__init__()
        self._mode = -1
        self._serial_port = None
        self.serial_thread = None
        self.web_thread = None
        self.timer = None
        self.previous_pos = None
        self.current_pos = None
        self._web_data = None
        self._distance_traveled = 0
        self._setup_web_thread()

        # Timer to check how many iBeacon packets are received when in listening mode
        self._found_count = 0
        self._found_addresses = set()
        self._found_timer = QTimer()
        self._found_timer.setInterval(1000)
        self._found_timer.timeout.connect(self._print_found_rate)
        self._found_timer.start()

        self._init_ui()

    @pyqtSlot()
    def _print_found_rate(self) -> None:
        """
        This method displays the number of beacons detected per second
        and the count of unique addresses found.
        """
        if self._mode != 1:
            return

        if self._serial_port is None:
            return

        if not self._serial_port.is_open:
            return

        if self._found_count == 0:
            return

        print(
            f"Beacons found per second: {self._found_count}, "
            f"unique {len(self._found_addresses)}"
        )
        self._found_count = 0
        self._found_addresses.clear()

    def _setup_web_thread(self) -> None:
        """
        Initialises and manages the web thread and a timer for periodic operations.
        """
        self.web_thread = WebThread()
        self.web_thread.start()

        # Connect signal to slot in background thread
        self.web_data_signal.connect(self.web_thread.worker.upload_data)
        self.web_thread.worker.data_posted.connect(
            lambda response: print(
                f"Response code {response.status_code}, {response.text}"
            )
        )

        self.timer = QTimer()
        self.timer.setInterval(5000)
        self.timer.timeout.connect(self.send_to_worker)
        self.timer.start()

    @pyqtSlot()
    def send_to_worker(self) -> None:
        """
        Sends web data to a worker process if conditions are met.
        """
        if self._web_data is None:
            return
        
        if self._serial_port is None:
            return
        
        if not self._serial_port.is_open:
            return
        
        self.web_data_signal.emit(self._web_data)
        self._web_data = None

    def _init_ui(self) -> None:
        """Construct and arrange all top-level UI elements."""
        self.setWindowTitle("Mini GUI by Jack & Eden")

        central_widget: QWidget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout: QVBoxLayout = QVBoxLayout(central_widget)

        # The console tab must be created first so that _log_message() can
        # write to it during the construction of other widgets.
        self._console_tab: QWidget = self._create_console_tab()

        connection_group: QGroupBox = self._create_connection_section()
        main_layout.addWidget(connection_group)

        self._main_tabs: QTabWidget = QTabWidget()
        self._main_tabs.addTab(self._create_grid_tab(), "Grid View")
        self._main_tabs.addTab(self._console_tab, "Console")
        self._main_tabs.addTab(self._create_beacon_command_tab(), "iBeacon Commands")
        self._main_tabs.addTab(self._create_ble_tab(), "BLE Ads")
        self._distance_widget = DistanceWidget()
        self._main_tabs.addTab(self._distance_widget, "Distance Testing")

        main_layout.addWidget(self._main_tabs)

        self._log_message("App Started. Please connect to a serial port.")

    def _create_console_tab(self) -> QWidget:
        """
        Build the console log tab.

        Returns
        -------
        QWidget
            Tab widget containing a read-only log area and a clear button.
        """
        widget: QWidget = QWidget()
        layout: QVBoxLayout = QVBoxLayout(widget)

        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFont(QFont("Courier", 11))
        layout.addWidget(self.log_text)

        clear_btn: QPushButton = QPushButton("Clear Log")
        clear_btn.clicked.connect(lambda: self.log_text.clear())
        layout.addWidget(clear_btn)

        return widget

    def _create_connection_section(self) -> QGroupBox:
        """
        Build the serial port connection controls.

        Returns
        -------
        QGroupBox
            Group box containing port selection, baud rate, connect button,
            and status / mode labels.
        """
        group: QGroupBox = QGroupBox("Serial Connection")
        layout: QHBoxLayout = QHBoxLayout()

        layout.addWidget(QLabel("Port:"))
        self._port_combo = QComboBox()
        self._port_combo.setMaxVisibleItems(10)
        self._port_combo.setMinimumWidth(300)
        self._refresh_ports()
        layout.addWidget(self._port_combo)

        refresh_btn: QPushButton = QPushButton("Refresh Ports")
        refresh_btn.clicked.connect(self._refresh_ports)
        layout.addWidget(refresh_btn)

        layout.addWidget(QLabel("Baudrate:"))
        self._baudrate_combo = QComboBox()
        self._baudrate_combo.addItems(["9600", "115200", "230400", "460800"])
        self._baudrate_combo.setCurrentText("115200")
        layout.addWidget(self._baudrate_combo)

        self._connect_btn = QPushButton("Connect")
        self._connect_btn.clicked.connect(self._toggle_connection)
        self._connect_btn.setStyleSheet(
            "background-color: #4CAF50; color: white; font-weight: bold;"
        )
        layout.addWidget(self._connect_btn)

        self._status_label = QLabel("Disconnected")
        self._status_label.setStyleSheet("color: red; font-weight: bold;")
        layout.addWidget(self._status_label)

        self._mode_label = QLabel("Mode: Unknown")
        self._mode_label.setStyleSheet("color: grey; font-weight: bold;")
        layout.addWidget(self._mode_label)

        layout.addStretch()
        group.setLayout(layout)
        return group

    def _create_grid_tab(self) -> QWidget:
        """
        Build the grid visualisation tab.

        Returns
        -------
        QWidget
            Tab containing a position-info legend and the GridWidget.
        """
        widget: QWidget = QWidget()
        layout: QVBoxLayout = QVBoxLayout(widget)

        # --- Position legend ---
        info_group: QGroupBox = QGroupBox("Position Info")
        info_layout: QHBoxLayout = QHBoxLayout()

        raw_dot_label = QLabel("●")
        raw_dot_label.setStyleSheet("color: #DC3232; font-size: 18px;")
        info_layout.addWidget(raw_dot_label)
        info_layout.addWidget(QLabel("Raw:"))
        self._raw_pos_label = QLabel("(-, -)")
        self._raw_pos_label.setFont(QFont("Courier", 10))
        info_layout.addWidget(self._raw_pos_label)

        info_layout.addSpacing(40)

        kalman_dot_label = QLabel("●")
        kalman_dot_label.setStyleSheet("color: #32DC32; font-size: 18px;")
        info_layout.addWidget(kalman_dot_label)
        info_layout.addWidget(QLabel("Kalman:"))
        self._kalman_pos_label = QLabel("(-, -)")
        self._kalman_pos_label.setFont(QFont("Courier", 10))
        info_layout.addWidget(self._kalman_pos_label)

        info_layout.addStretch()
        info_group.setLayout(info_layout)
        layout.addWidget(info_group)

        # --- Grid canvas ---
        self._grid_widget = GridWidget()
        self._grid_widget.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        layout.addWidget(self._grid_widget)

        return widget

    def _create_ble_cmds(self) -> QWidget:
        """
        Build the BLE start/stop control group.

        Returns
        -------
        QGroupBox
            Group box containing "Start Listening" and "Stop Listening" buttons.
        """
        ble_group: QGroupBox = QGroupBox("BLE Controls")
        ble_layout: QHBoxLayout = QHBoxLayout()

        start_btn: QPushButton = QPushButton("Start Listening")
        start_btn.setStyleSheet(
            "background-color: #4CAF50; color: white; font-weight: bold; padding: 8px;"
        )
        start_btn.clicked.connect(lambda: self._send_json_command("ble", "start"))
        ble_layout.addWidget(start_btn)

        stop_btn: QPushButton = QPushButton("Stop Listening")
        stop_btn.setStyleSheet(
            "background-color: #f44336; color: white; font-weight: bold; padding: 8px;"
        )
        stop_btn.clicked.connect(lambda: self._send_json_command("ble", "stop"))
        ble_layout.addWidget(stop_btn)

        ble_layout.addStretch()
        ble_group.setLayout(ble_layout)
        return ble_group

    def _create_beacon_add_tab(self) -> QWidget:
        """
        Build the "Add iBeacon" form tab.

        Returns
        -------
        QWidget
            Tab containing input fields for all iBeacon attributes and a
            submit button.
        """
        add_tab: QWidget = QWidget()
        add_layout: QGridLayout = QGridLayout(add_tab)

        add_layout.addWidget(QLabel("Name:"), 0, 0)
        self._add_name = QLineEdit()
        self._add_name.setPlaceholderText("4011-Z")
        add_layout.addWidget(self._add_name, 0, 1)

        add_layout.addWidget(QLabel("MAC:"), 0, 2)
        self._add_mac = QLineEdit()
        self._add_mac.setPlaceholderText("AA:BB:CC:DD:EE:FF")
        add_layout.addWidget(self._add_mac, 0, 3)

        add_layout.addWidget(QLabel("Major:"), 1, 0)
        self._add_major = QLineEdit()
        self._add_major.setPlaceholderText("0x0000")
        add_layout.addWidget(self._add_major, 1, 1)

        add_layout.addWidget(QLabel("Minor:"), 1, 2)
        self._add_minor = QLineEdit()
        self._add_minor.setPlaceholderText("0x0000")
        add_layout.addWidget(self._add_minor, 1, 3)

        add_layout.addWidget(QLabel("X (mm):"), 2, 0)
        self._add_x = QLineEdit()
        self._add_x.setPlaceholderText("0.0")
        add_layout.addWidget(self._add_x, 2, 1)

        add_layout.addWidget(QLabel("Y (mm):"), 2, 2)
        self._add_y = QLineEdit()
        self._add_y.setPlaceholderText("0.0")
        add_layout.addWidget(self._add_y, 2, 3)

        add_layout.addWidget(QLabel("Cali:"), 3, 0)
        self._add_cali = QLineEdit()
        self._add_cali.setPlaceholderText("-57")
        add_layout.addWidget(self._add_cali, 3, 1)

        add_layout.addWidget(QLabel("Left:"), 3, 2)
        self._add_left = QLineEdit()
        self._add_left.setPlaceholderText("NULL")
        add_layout.addWidget(self._add_left, 3, 3)

        add_layout.addWidget(QLabel("Right:"), 4, 0)
        self._add_right = QLineEdit()
        self._add_right.setPlaceholderText("NULL")
        add_layout.addWidget(self._add_right, 4, 1)

        add_submit = QPushButton("Add iBeacon")
        add_submit.setStyleSheet(
            "background-color: #2196F3; color: white; font-weight: bold; padding: 6px;"
        )
        add_submit.clicked.connect(self._cmd_beacon_add)
        add_layout.addWidget(add_submit, 5, 0, 1, 4)

        return add_tab

    def _create_delete_tab(self) -> QWidget:
        """
        Build the "Delete iBeacon" form tab.

        Returns
        -------
        QWidget
            Tab containing name, major, and minor input fields plus a delete
            button.
        """
        del_tab: QWidget = QWidget()
        del_layout: QGridLayout = QGridLayout(del_tab)

        del_layout.addWidget(QLabel("Name:"), 0, 0)
        self._del_name = QLineEdit()
        self._del_name.setPlaceholderText("4011-Y")
        del_layout.addWidget(self._del_name, 0, 1)

        del_layout.addWidget(QLabel("Major:"), 1, 0)
        self._del_major = QLineEdit()
        self._del_major.setPlaceholderText("0x0000")
        del_layout.addWidget(self._del_major, 1, 1)

        del_layout.addWidget(QLabel("Minor:"), 2, 0)
        self._del_minor = QLineEdit()
        self._del_minor.setPlaceholderText("0x0000")
        del_layout.addWidget(self._del_minor, 2, 1)

        del_submit = QPushButton("Delete iBeacon")
        del_submit.setStyleSheet(
            "background-color: #f44336; color: white; font-weight: bold; padding: 6px;"
        )
        del_submit.clicked.connect(self._cmd_beacon_delete)
        del_layout.addWidget(del_submit, 3, 0, 1, 2)

        # Push form widgets to the top of the tab.
        del_layout.setRowStretch(4, 1)

        return del_tab

    def _create_beacon_command_tab(self) -> QWidget:
        """
        Build the iBeacon command tab (Add / Delete / View sub-tabs).

        Returns
        -------
        QWidget
            Tab widget containing an inner QTabWidget with three sub-tabs.
        """
        widget: QWidget = QWidget()
        layout: QHBoxLayout = QHBoxLayout(widget)

        ibeacon_tabs: QTabWidget = QTabWidget()
        ibeacon_tabs.addTab(self._create_beacon_add_tab(), "Add")
        ibeacon_tabs.addTab(self._create_delete_tab(), "Delete")

        self._beacon_view = BeaconViewWidget(on_send_cmd=self._send_json_command)
        ibeacon_tabs.addTab(self._beacon_view, "View")

        layout.addWidget(ibeacon_tabs)
        return widget

    def _create_ble_tab(self) -> QWidget:
        """
        Build the BLE advertisements tab.

        Returns
        -------
        QWidget
            Tab containing BLE start/stop controls, a scrolling advertisement
            log, and a clear button.
        """
        widget: QWidget = QWidget()
        layout: QVBoxLayout = QVBoxLayout(widget)

        ble_group: QGroupBox = self._create_ble_cmds()
        layout.addWidget(ble_group)

        self.ble_ad_log = QTextEdit()
        self.ble_ad_log.setReadOnly(True)
        self.ble_ad_log.setFont(QFont("Courier", 11))
        layout.addWidget(self.ble_ad_log)

        clear_btn: QPushButton = QPushButton("Clear BLE Log")
        clear_btn.clicked.connect(lambda: self.ble_ad_log.clear())
        layout.addWidget(clear_btn)

        return widget

    def _cmd_beacon_add(self) -> None:
        """
        Validate the Add-iBeacon form and send the add command.

        Shows a warning dialogue if any required field is empty.
        """
        opts = [
            self._add_name.text(),
            self._add_mac.text(),
            self._add_major.text(),
            self._add_minor.text(),
            self._add_x.text(),
            self._add_y.text(),
            self._add_cali.text(),
            self._add_left.text() or "NULL",
            self._add_right.text() or "NULL",
        ]
        if any(o == "" for o in opts):
            QMessageBox.warning(
                self, "Input Error", "Please fill in all required fields."
            )
            return
        self._send_json_command("iBeacon", "add", opts)

    def _cmd_beacon_delete(self) -> None:
        """
        Validate the Delete-iBeacon form and send the delete command.

        Shows a warning dialogue if any fields are empty or missing.
        """
        name = self._del_name.text()
        major = self._del_major.text()
        minor = self._del_minor.text()
        if not name or not major or not minor:
            QMessageBox.warning(
                self, "Input Error", "Name, Major and Minor are required."
            )
            return
        self._send_json_command("iBeacon", "delete", [name, major, minor])

    @pyqtSlot()
    def _refresh_ports(self) -> None:
        """
        Scan for available serial ports and populate the port combo box.

        Ports without a valid device path or a meaningful description are
        excluded.  Logs the outcome regardless of whether ports were found.
        """
        self._port_combo.clear()
        try:
            ports = list(serial.tools.list_ports.comports())
            valid_ports = []

            for port in ports:
                if not port.device:
                    continue

                if not port.description or port.description.strip().upper() == "N/A":
                    continue

                valid_ports.append(port)
                display_text: str = f"{port.device} ({port.description[:40]})"
                self._port_combo.addItem(display_text, port.device)

            if not valid_ports:
                self._port_combo.addItem("No valid ports found", None)
                self._log_message("No valid serial ports detected.")

        except serial.SerialException as e:
            self._port_combo.addItem("Error scanning ports", None)
            self._log_message(f"Error scanning ports: {e}")

    @pyqtSlot()
    def _toggle_connection(self) -> None:
        """Connect to or disconnect from the currently selected serial port."""
        if self._serial_port and self._serial_port.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self) -> None:
        """
        Open the selected serial port and start the reader thread.

        Reads the port device path and baud rate from the UI controls.
        Shows a critical error dialogue if the port cannot be opened.
        """
        port: Optional[str] = self._port_combo.currentData()
        baudrate: int = int(self._baudrate_combo.currentText())

        if not port:
            QMessageBox.warning(self, "Connection Error", "No valid port selected.")
            return

        try:
            self._serial_port = serial.Serial(port, baudrate, timeout=0.1)
            self._log_message(f"Connected to {port} at {baudrate} baud")

            # Start the background UART listener thread.
            self.serial_thread = SerialReader(self._serial_port)
            self.serial_thread.port_error_signal.connect(self._port_disconnected)
            self.serial_thread.data_received_signal.connect(self._handle_incoming_data)
            self.serial_thread.data_parse_error_signal.connect(self._handle_parse_error)
            self.serial_thread.start()

            # Update connection UI to reflect the connected state.
            self._status_label.setText("Connected")
            self._status_label.setStyleSheet("color: green; font-weight: bold;")
            self._connect_btn.setText("Disconnect")
            self._connect_btn.setStyleSheet(
                "background-color: #f44336; color: white; font-weight: bold;"
            )

        except serial.SerialException as e:
            QMessageBox.critical(self, "Connection Error", f"Failed to connect: {e}")
            self._log_message(f"Connection failed: {e}")

    def _disconnect(self, message: str = "Disconnected from serial port") -> None:
        """Stop the reader thread and close the serial port."""
        if self.serial_thread:
            self.serial_thread.stop()
            self.serial_thread.wait()
            self.serial_thread = None

        if self._serial_port and self._serial_port.is_open:
            self._serial_port.close()
            self._log_message(message)

        # Update connection UI to reflect the disconnected state.
        self._status_label.setText("Disconnected")
        self._status_label.setStyleSheet("color: red; font-weight: bold;")
        self._connect_btn.setText("Connect")
        self._connect_btn.setStyleSheet(
            "background-color: #4CAF50; color: white; font-weight: bold;"
        )

        # Reset the counters
        self._distance_traveled = 0
        self.current_pos = None
        self.previous_pos = None

    @pyqtSlot()
    def _port_disconnected(self) -> None:
        """
        Handle an unexpected serial port disconnection.

        Called via SerialReader.port_error_signal.  Cleans up the
        connection state and informs the user.
        """
        self._disconnect("Unexpected disconnection from serial port")
        QMessageBox.critical(
            self, "Serial Port Error", "Serial port has been disconnected"
        )

    def _set_mode_label(self, mode: str) -> None:
        """
        Update the mode label text and colour to reflect the current firmware mode.

        Parameters
        ----------
        mode : str
            "listen" for BLE sniffing mode, "standard" for GATT mode,
            or any other value to display "Unknown".
        """
        if mode == "listen":
            self._mode_label.setText("Mode: Listening")
            self._mode_label.setStyleSheet("color: #2196F3; font-weight: bold;")
        elif mode == "standard":
            self._mode_label.setText("Mode: Standard")
            self._mode_label.setStyleSheet("color: #4CAF50; font-weight: bold;")
        else:
            self._mode_label.setText("Mode: Unknown")
            self._mode_label.setStyleSheet("color: grey; font-weight: bold;")

    @pyqtSlot(str)
    def _handle_parse_error(self, message: str) -> None:
        """Route non-JSON serial output to the appropriate handler."""
        if "[DISTANCE]" in message:
            self._parse_distance_message(message)
        else:
            self._log_message(message)

    def _parse_distance_message(self, message: str) -> None:
        """Messages tagged [DISTANCE] carry the format: [DISTANCE]||<name>||<value>"""
        try:
            name, dist = message.split("||", 1)[1].split("||", 1)
            self._distance_widget.update_distance(name, float(dist))
        except (IndexError, ValueError) as e:
            print(f"Error parsing distance message: {e}")

    def _log_message(self, message: str) -> None:
        """
        Append a timestamped message to the console log and auto-scroll.

        Also inspects the message for known firmware status strings and
        updates the mode label accordingly:

        - "Sniffing for iBeacons" → Listening mode.
        - "Looking for GATT connection" → Standard mode.

        Parameters
        ----------
        message : str
            The message to display in the console log.
        """
        self._set_firmware_mode(message)
        timestamp: str = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.log_text.append(f"[{timestamp}] {message}")

        # Keep the most recent entry visible.
        scrollbar = self.log_text.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())

    def _set_firmware_mode(self, message: str) -> None:
        """Set the mode of the firmware chip"""
        if "Sniffing for iBeacons" in message:
            self._set_mode_label("listen")
            self._mode = 1
        elif "Looking for GATT connection" in message:
            self._set_mode_label("standard")
            self._mode = 0

    def _ble_log_message(self, message: str) -> None:
        """
        Append a timestamped message to the BLE advertisement log and auto-scroll.

        Parameters
        ----------
        message : str
            The message to display in the BLE advertisement log.
        """
        timestamp: str = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.ble_ad_log.append(f"[{timestamp}] {message}")
        # Keep the most recent entry visible.
        scrollbar = self.ble_ad_log.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())

    @pyqtSlot(dict)
    def _handle_incoming_data(self, data: dict) -> None:
        """
        Route a parsed JSON packet to the appropriate handler.

        Expected packet formats::

            # BLE advertisement detected by the scanner.
            {"cmd": "found", "name": "<n>", "major": "0xXXXX",
             "minor": "0xXXXX", "addr": "<addr>", "rssi": "<rssi>"}

            # Position estimate (raw or Kalman-filtered).
            {"cmd": "pos", "sub": "raw", "opts": ["x", "y", "time"]}
            {"cmd": "pos", "sub": "kalman", "opts": ["x", "y", "time"]}

            # iBeacon view protocol packets.
            {"cmd": "view", "sub": "start" | "node" | "end", ...}

        Parameters
        ----------
        data : dict
            Decoded JSON object received from the serial port.
        """
        if "cmd" not in data:
            self._log_message(f"Received invalid data: {data}")
            return

        cmd = data["cmd"]

        if cmd == "found":
            self._found_count += 1
            self._found_addresses.add(data["addr"])
            self._ble_log_message(
                f"iBeacon Found: Name: {data['name']}, Major: {data['major']}, "
                f"Minor: {data['minor']}, ble_addr: {data['addr']}, "
                f"RSSI: {data['rssi']}"
            )
        elif cmd == "pos":
            self._parse_position_data(data["sub"], data.get("opts", []))
        elif cmd == "view":
            self._beacon_view.handle_view_packet(data["sub"], data.get("opts", []))
        else:
            self._log_message(f"Received unknown command: {data}")

    def _parse_position_data(self, sub: str, opts: list[str]) -> None:
        """
        Parses position-related data based on the provided sub-command and options. Updates the
        current position data or stores readings based on the sub-command.

        Parameters
        ----------
        sub: The sub-command specifying the type of data to parse. Expected values are
            "start", "raw", "kalman", "rssi", or "stop".
        opts: A list of string options relevant to the sub-command.
        """
        # Handle the "start" sub-command: reset current position before accumulating new data.
        if sub == "start":
            self.previous_pos = self.current_pos
            self.current_pos = {}
            now = datetime.now()
            self.current_pos["time"] = now
            self.current_pos["time_str"] = now.strftime("%H:%M:%S.%f")[:-3]
        elif self.current_pos is None:
            return  # Not yet initialised; drop packets until a "start" sub-command is seen.
        elif sub == "raw" or sub == "kalman":
            self._update_position_data(sub, opts, self.current_pos["time"])
        elif sub == "rssi":
            self.current_pos["rssi"] = []
            for i in range(0, len(opts), 2):
                name = opts[i].lower().replace("-", "")
                reading = opts[i + 1]
                try:
                    # Format required by the web server.
                    self.current_pos["rssi"].append(
                        {"variable": name, "value": int(reading), "unit": "dBm"}
                    )
                except ValueError:
                    self._log_message(f"Invalid RSSI value for {name}: {reading}")
        elif sub == "stop":
            if self.previous_pos is not None:
                self._create_web_packet()

    def _create_web_packet(self) -> None:
        """
        Generates a web packet with the current positional and telemetry data.
        """
        try:
            # Calculate the remaining fields needed to construct the web server payload.
            x_squared = (
                self.current_pos["kalman"][0] - self.previous_pos["kalman"][0]
            ) ** 2
            y_squared = (
                self.current_pos["kalman"][1] - self.previous_pos["kalman"][1]
            ) ** 2
            distance_step = math.sqrt(x_squared + y_squared)
            self._distance_traveled += distance_step
            time_delta = self.current_pos["time"] - self.previous_pos["time"]
            time_step = time_delta.total_seconds()
            if time_step <= 0.0:
                return

            velocity = distance_step / time_step

            # Build the list of dicts required by the web server.
            # Structure: [{"variable": "4011#", "value": <reading>, "unit": "dBm"}, ...]
            self._web_data = self.current_pos["rssi"]
            self._web_data.append(
                {"variable": "distance", "value": self._distance_traveled, "unit": "mm"}
            )
            self._web_data.append(
                {"variable": "velocity", "value": velocity, "unit": "mm/s"}
            )

        except (KeyError, ZeroDivisionError) as e:
            # Any errors here are non-fatal; skip this upload cycle and continue.
            print(f"Error in create_web_packet: {e}")
            self._web_data = None

    def _update_position_data(self, sub: str, data: list, timestamp: str) -> None:
        """
        Update the position labels and grid dot for a raw or Kalman estimate.

        Parameters
        ----------
        data : dict
            Position packet with keys "sub" ("raw" or "kalman")
            and "opts" containing [x, y, time].
        timestamp : str
            Pre-formatted timestamp string to display alongside the coordinates.
        """
        try:
            x, y, _ = data
            x = round(float(x))
            y = round(float(y))
        except ValueError:
            self._log_message(f"Invalid position data: {data}")
            return

        if sub == "raw":
            self.current_pos["raw"] = (x, y)
            self._raw_pos_label.setText(f"({x}, {y}), Sample received: {timestamp}")
            self._grid_widget.set_raw_position(x, y)
        elif sub == "kalman":
            self.current_pos["kalman"] = (x, y)
            self._kalman_pos_label.setText(f"({x}, {y}), Sample received: {timestamp}")
            self._grid_widget.set_kalman_position(x, y)

    def _send_command(self, command: str) -> None:
        """
        Write a raw string command to the open serial port.

        A newline character is appended before sending. Logs an error and
        shows a dialogue if the port is not open.

        Parameters
        ----------
        command : str
            The command string to transmit.
        """
        if self._serial_port and self._serial_port.is_open:
            try:
                self._serial_port.write(f"{command}\n".encode())
            except serial.SerialException as e:
                self._log_message(f"Error sending command: {e}")
        else:
            self._log_message("Serial port not connected")
            QMessageBox.critical(
                self,
                "Serial Port Error",
                "No serial port has been opened.\n"
                "Please select a port from the dropdown and try again.",
            )

    def _send_json_command(self, cmd: str, sub: str, opts: list = None) -> None:
        """
        Serialise a command as JSON and transmit it over the serial link.

        Parameters
        ----------
        cmd : str
            Top-level command identifier (e.g. "iBeacon", "ble").
        sub : str
            Sub-command identifier (e.g. "add", "start").
        opts : list, optional
            Additional parameters to include in the "opts" field.
            Defaults to an empty list when not provided.
        """
        payload = {"cmd": cmd, "sub": sub, "opts": opts or []}
        self._send_command(json.dumps(payload))


def send_to_webserver(data: list) -> "requests.Response | None":
    """
    POST a JSON payload to a remote web server.

    Parameters
    ----------
    data : dict
        Data to serialise as the JSON request body.

    Returns
    -------
    requests.Response or None
        The HTTP response object on success, or None if a connection error
        occurs.
    """
    url = "https://api.eu-w1.tago.io/data"

    headers_eden = {
        "Device-Token": "29b21e40-64a8-4cfb-a5e1-ba58996dd68e",
        "Content-Type": "application/json",
    }

    headers_jack = {
        "Device-Token": "035d066b-3b12-47da-afee-dd66ec0c876b",
        "Content-Type": "application/json",
    }

    try:
        requests.post(url, json=data, headers=headers_jack)  # Ignore response
        response = requests.post(url, json=data, headers=headers_eden)
        return response
    except Exception as e:
        print(f"Web server connection error: {e}")
        return None


if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    controller: Controller = Controller()
    controller.show()

    sys.exit(app.exec_())
