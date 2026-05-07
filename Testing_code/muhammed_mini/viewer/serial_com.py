import json
import serial
import queue
import sys
import threading
import time
import math

from datetime import datetime
from enum import IntEnum
from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import ASYNCHRONOUS

influx_bucket = "get-started"
influx_org = "MA,JB,4011"
influx_token = "nope"
influx_url = "https://us-east-1-1.aws.cloud2.influxdata.com/"
influx_client = InfluxDBClient(url=influx_url, token=influx_token, org=influx_org)
write_api = influx_client.write_api(write_options=ASYNCHRONOUS)

class Queues(IntEnum):
    SNIFFER = 0
    UNFILTERED = 1
    KALMAN = 2
    IBEACONS = 3
    ARBITRARY = 4
    LOG = 5

class Serial_Channel():
    def __init__(self, port, baud):
        self.tx_data_queue = queue.Queue()
        self.queues = [queue.Queue() for i in Queues]

        self.port_opened = False
        self.port = port
        self.baud = baud
        
        self.kalman_position = None
        self.kalman_time = time.time_ns() // 1_000_000
        self.distance = 0


    def open_serial(self) -> serial.Serial:
        """
            Attempts to open a serial connection. If failed keeps retrying. 

            Params:
                None
            Returns:
                serial object
        """
        self.port_opened = False

        while not self.port_opened:
            try:
                ser = serial.Serial(self.port, self.baud, timeout=1)
            except Exception:
                print(f"Cannot open: {self.port}", file=sys.stderr)
                print("Trying Again in 1 second...", file=sys.stderr)
                time.sleep(1)
            else:
                self.port_opened = True
                print(f"Successfully opened: {self.port}", file=sys.stderr)
        return ser


    def serial_handle_thread(self):
        """
            Callback for serial thread. Continously trys to opening a 
            serial connection. If succssesful, it will filter any incoming data 
            or logs and stores them in thier respective data queues.

            Params:
                None
            Returns:
                None
        """
        ser = self.open_serial()
        while True:
            try:
                if not self.tx_data_queue.empty():
                    data = self.tx_data_queue.get().encode('utf-8')
                    ser.write(data)
                line = ser.readline()
            except Exception:
                ser.close()
                ser = self.open_serial()
            else:
                self.parse_data(line)

    def update_speed_and_distance(self, json):
        time_now = time.time_ns() / 1_000_000
        if self.kalman_position is not None:
            time_diff = time_now - self.kalman_time
            x_diff = abs(self.kalman_position["x"] - json["x"])
            y_diff = abs(self.kalman_position["y"] - json["y"])

            speed = 1000 * math.sqrt(x_diff ** 2 + y_diff ** 2) / time_diff
            self.distance = self.distance + (speed / 1000) * time_diff

            speed_p = Point("my_meas").tag("location", "Mobile").field("Speed", speed)
            dist_p = Point("my_meas").tag("location", "Mobile").field("Distance",self.distance)
            write_api.write(bucket=influx_bucket, org=influx_org, record=speed_p, write_precision=WritePrecision.MS)
            write_api.write(bucket=influx_bucket, org=influx_org, record=dist_p, write_precision=WritePrecision.MS)

        self.kalman_time = time_now
        self.kalman_position = json

    def parse_data(self, raw_data):
        line = raw_data.decode(errors='ignore').strip()

        if len(line) < 3:
            return

        data_type = line[0]
        data = line[3:]
        
        try:
            time_now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            if data_type == 'L':
                data_dict = {"log": data, "time": time_now}
                self.queues[Queues.LOG].put(data_dict)
            elif data_type in "AIKPS":
                json_data = json.loads(data)
                json_data["time"] = time_now

                match data_type:
                    case 'A':
                        self.queues[Queues.ARBITRARY].put(json_data)
                    case 'I':
                        self.queues[Queues.IBEACONS].put(json_data)
                    case 'K':
                        self.update_speed_and_distance(json_data)
                        if not self.is_queue_empty(Queues.KALMAN):
                            self.queues[Queues.KALMAN].get_nowait()

                        self.queues[Queues.KALMAN].put(json_data)
                    case 'P':
                        if not self.is_queue_empty(Queues.UNFILTERED):
                            self.queues[Queues.UNFILTERED].get_nowait()
                        self.queues[Queues.UNFILTERED].put(json_data)
                    case 'S':
                        self.queues[Queues.SNIFFER].put(json_data)
        except json.decoder.JSONDecodeError:
            return

    def get_next_queue_item(self, queue_name: Queues):
        return self.queues[queue_name].get()

    def is_queue_empty(self, queue_name: Queues):
        return self.queues[queue_name].empty()

    def serial_write(self, data : str):
        """
            Writes data to the serial device

            Params:
                data: data to be sent to device
            Returns:
                None
        """
        self.tx_data_queue.put(data)

    def serial_device_is_open(self):
        return self.port_opened

    def serial_start(self):
        """
            Starts serial thread

            Params:
                None
            Returns:
                None
        """
        threading.Thread(target=self.serial_handle_thread, daemon=True).start()
