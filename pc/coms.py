"""
start up script

Copyright (c) 2026 Jack Cairns, Eden Mehr, Muhammed Abdilrahmin
"""

# Fix dropdown boxes not staying open on some Linux desktop environments.
import os

os.environ["QT_QPA_PLATFORM"] = "xcb"


import json
from datetime import datetime
import serial
import serial.tools.list_ports
import sys
from typing import Optional
from PyQt5.QtCore import QThread, pyqtSignal, pyqtSlot
from PyQt5.QtWidgets import (
    QApplication,
    QWidget,
    QVBoxLayout,
    QMessageBox,
    QPushButton,
    QComboBox,
    QLabel,
    QGroupBox,
    QHBoxLayout,
)

class Controller(QWidget):
    """
    Main application window and central controller.

    Manages the serial connection lifecycle, routes incoming serial data to
    the appropriate UI components, and owns all top-level widgets.
    """

    def __init__(self):
        """
        Initialise GUI
        """

        super().__init__()
        self._current_time = 0
        self._serial_port = None
        self.serial_thread = None

        self.setWindowTitle("COMS4011 -- Magenta Mimas")
        self.setGeometry(100, 100, 300, 200)

        # Create widgets
        self.label = QLabel("Select Sampling Time (ms):")

        self.combo_box = QComboBox()
        self.combo_box.setCurrentText("100")
        self.combo_box.addItems(["50", "100", "250", "500", "750", "1000"])

        self.pulse = QPushButton("Update Sampling Time")
        self.stamp = QPushButton("Sync Timestamps")

        # Connect buttons to functions
        self.pulse.clicked.connect(self.pulse_clicked)
        self.stamp.clicked.connect(self.stamp_clicked)

        # Layout
        layout = QVBoxLayout()
        con = self._create_connection_section()
        layout.addWidget(con)
        layout.addWidget(self.label)
        layout.addWidget(self.combo_box)
        layout.addWidget(self.pulse)
        layout.addWidget(self.stamp)

        self.setLayout(layout)

    def pulse_clicked(self):
        """
        Send new sampling time.
        """
        selected = self.combo_box.currentText()
        self.update_pulse_delay(int(selected))

    def stamp_clicked(self):
        """
        Send timestamp for synchronisation.
        """
        self._current_time = datetime.now()
        self.send_timestamp()

    def send_timestamp(self) -> None:
        """
        Transmit timestamp to base.
        """

        payload = {
            "cmd":2,
            "pulse":0,
            "time": self._current_time.timestamp()
        }
        cmd = json.dumps(payload)

        if self._serial_port and self._serial_port.is_open:
            try:
                self._serial_port.write(f"{cmd}\n".encode())
                print(cmd)
            except serial.SerialException as e:
                print(f"Error sending command: {e}")
        else:
            print("Serial Port not connected")

    def update_pulse_delay(self, delay: int) -> None:
        """
        Updates sampling time given user input.
        """

        payload = {
            "cmd":1,
            "pulse":delay,
            "time": 0
        }
        cmd = json.dumps(payload)

        if self._serial_port and self._serial_port.is_open:
            try:
                self._serial_port.write(f"{cmd}\n".encode())
                print(cmd)
            except serial.SerialException as e:
                print(f"Error sending command: {e}")
        else:
            print("Serial Port not connected")

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

        self._connect_btn = QPushButton("Connect")
        self._connect_btn.clicked.connect(self._toggle_connection)
        self._connect_btn.setStyleSheet(
            "background-color: #4CAF50; color: white; font-weight: bold;"
        )
        layout.addWidget(self._connect_btn)

        self._status_label = QLabel("Disconnected")
        self._status_label.setStyleSheet("color: red; font-weight: bold;")
        layout.addWidget(self._status_label)

        layout.addStretch()
        group.setLayout(layout)
        return group
    
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

        except serial.SerialException as e:
            self._port_combo.addItem("Error scanning ports", None)

    @pyqtSlot()
    def _toggle_connection(self) -> None:
        """
        Connect to or disconnect from the currently selected serial port.
        """

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
        baudrate: int = 115200

        if not port:
            QMessageBox.warning(self, "Connection Error", "No valid port selected.")
            return

        try:
            self._serial_port = serial.Serial(port, baudrate, timeout=0.1)

            # Start the background UART listener thread.
            self.serial_thread = SerialReader(self._serial_port)
            self.serial_thread.port_error_signal.connect(self._port_disconnected)
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

    def _disconnect(self, message: str = "Disconnected from serial port") -> None:
        """
        Stop the reader thread and close the serial port.
        """

        if self.serial_thread:
            self.serial_thread.stop()
            self.serial_thread.wait()
            self.serial_thread = None

        if self._serial_port and self._serial_port.is_open:
            self._serial_port.close()

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

class SerialReader(QThread):
    """
    Background thread that reads JSON-formatted lines from a serial port.
    "port_error_signal" is emitted if the port is unexpectedly disconnected.

    Signals
    port_error_signal :
        Emitted when a "SerialException" is encountered during reading.
    """

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
                            # TODO: send to webserver
                            print(data)
                        else:
                            # print(f"Unexpected JSON type: {line}")
                            pass
                    except json.JSONDecodeError:
                        # Non-JSON output from firmware; pass to logger.
                        # print(f"Non-JSON type: {line}")
                        pass
            except UnicodeDecodeError as e:
                print(f"Couldn't decode data: {e}")
            except serial.SerialException as e:
                print(f"Serial port disconnected: {e}")
                self.port_error_signal.emit()

            self.msleep(10)

    def stop(self) -> None:
        """
        Signal the thread to stop and block until it exits.
        """

        self._running = False
        self.wait()


if __name__ == "__main__":

    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    window = Controller()
    window.show()

    sys.exit(app.exec_())
