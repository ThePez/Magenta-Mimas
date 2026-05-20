"""
start up script
"""

import json
from datetime import datetime
import serial
import threading
import serial.tools.list_ports

# open coms port on startup
# send jack (base) json of key: "time", value: datetime epoch time
# receive json data from base

class SerialReader(threading.Thread):
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

    def __init__(self, serial_port: serial.Serial) -> None:
        """
        Initialise the reader thread.

        Parameters
        -----------
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
                            # send to webserver
                            continue
                        else:
                            print(f"Unexpected JSON type: {line}")
                    except json.JSONDecodeError:
                        # Non-JSON output from firmware, pass to logger
                        print(f"Unexpected JSON type: {line}")
            except UnicodeDecodeError as e:
                print(f"Couldn't decode data: {e}")
            except serial.SerialException as e:
                print(f"Serial port disconnected: {e}")
                Controller._disconnect()

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
            print("Connected!")

            # start the background UART listener thread
            self.serial_thread = SerialReader(self._serial_port)
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

if __name__ == "__main__":
    controller: Controller = Controller()
    controller._connect()


