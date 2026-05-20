"""
start up script
"""

import json
from datetime import datetime
import serial
import serial.tools.list_ports
import sys
from PyQt5.QtCore import QThread
from PyQt5.QtWidgets import (
    QApplication,
    QWidget,
    QPushButton,
    QComboBox,
    QVBoxLayout,
    QLabel,
)

class Window(QWidget):
    """
    Initialise the GUI.
    """

    def __init__(self):

        super().__init__()
        self.controller : Controller = Controller()
        self.controller._get_port()
        self.controller._connect()

        self.setWindowTitle("COMS4011 -- Magenta Mimas")
        self.setGeometry(100, 100, 300, 200)

        # Create widgets
        self.label = QLabel("Select an option:")

        self.combo_box = QComboBox()
        self.combo_box.setCurrentText("100")
        self.combo_box.addItems(["50", "100", "250", "500"])

        self.pulse = QPushButton("Update Sampling Time")
        self.stamp = QPushButton("Sync Timestamps")

        # Connect buttons to functions
        self.pulse.clicked.connect(self.pulse_clicked)
        self.stamp.clicked.connect(self.stamp_clicked)

        # Layout
        layout = QVBoxLayout()
        layout.addWidget(self.label)
        layout.addWidget(self.combo_box)
        layout.addWidget(self.pulse)
        layout.addWidget(self.stamp)

        self.setLayout(layout)

    def pulse_clicked(self):
        selected = self.combo_box.currentText()
        self.controller._update_pulse_delay(int(selected))

    def stamp_clicked(self):
        self.controller._get_current_time()
        self.controller._send_timestamp()

class SerialReader(QThread):
    """
    Background thread that reads JSON-formatted lines from a serial port.
    """

    def __init__(self, serial_port: serial.Serial, disconnected) -> None:
        """
        Initialise the reader thread.

        Parameters
        -----------
        serial_port : serial.Serial
            An open serial port to read from.
        """

        super().__init__()
        self._disconnect = disconnected
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
                            #TODO: send to webserver
                            print(data)
                        else:
                            # print(line)
                            pass
                    except json.JSONDecodeError:
                        # Non-JSON output from firmware
                        # print(line)
                        pass
            except UnicodeDecodeError as e:
                print(f"Couldn't decode data: {e}")
            except serial.SerialException as e:
                print(f"Serial port disconnected: {e}")
                self._disconnect()

            self.msleep(10)

    def stop(self) -> None:
        """
        Signal the thread to stop and block until it exits.
        """

        self._running = False
        self.wait()

class Controller:
    """
    Main application window and central controller, manages the 
    serial connection lifecycle.
    """

    def __init__(self) -> None:
        self._serial_port = None
        self._port = None
        self.serial_thread = None
        self.current_time = datetime.now()

    def _get_port(self) -> None:
        """
        Scan for available serial ports and populate the port combo box.

        Ports without a valid device path or a meaningful description are
        excluded. Prints the outcome regardless of whether ports were found.
        """

        try:
            ports = list(serial.tools.list_ports.comports())

            for port in ports:
                print(port)
                if port.description.strip().upper() == "WAVES HID":
                    self._port = port.device
                    return
            
            print("No valid serial ports detected.")

        except serial.SerialException as e:
            print(f"Error scanning ports: {e}")

    def _connect(self) -> None:
        """
        Open the first serial port that works, and start the reader thread.
        """

        try:
            self._serial_port = serial.Serial(self._port, 115200, timeout=0.1)

            # start the background UART listener thread
            self.serial_thread = SerialReader(self._serial_port, self._disconnect)
            self.serial_thread.start()

        except serial.SerialException as e:
            print(f"Connection failed: {e}")

    def _disconnect(self) -> None:
        """
        Stop the reader thread and close the serial port
        """

        if self.serial_thread:
            self.serial_thread.stop()
            self.serial_thread.wait()
            self.serial_thread = None

        if self._serial_port and self._serial_port.is_open:
            self._serial_port.close()
            print("Disconnected from serial port")

    def _get_current_time(self) -> None:
        self.current_time = datetime.now()

    def _send_timestamp(self) -> None:
        """
        Transmit timestamp to base.
        """

        self._get_current_time()
        payload = {
            "cmd":2,
            "pulse":0,
            "time": self.current_time.timestamp()
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

    def _update_pulse_delay(self, delay: int) -> None:
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

if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    window = Window()
    window.show()

    sys.exit(app.exec_())

