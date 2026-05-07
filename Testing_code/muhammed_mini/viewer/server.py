from flask import Flask, render_template, request, redirect, session, jsonify, flash, url_for
from serial_com import Queues, Serial_Channel
from datetime import datetime
import json
import sys
import time

app = Flask(__name__)
app.secret_key = "kawgjabdeidjw93"
serial_dev = Serial_Channel("/dev/ttyACM0", 115200)

default_beacons = ["4011-A", "4011-B", "4011-C", "4011-D", "4011-E", "4011-F",
    "4011-G", "4011-H", "4011-I", "4011-J", "4011-K", "4011-L", "4011-M"]

@app.before_request
def check_serial():
    if request.path in ["/static", "/rage"]:
        return
    if not serial_dev.serial_device_is_open():
        return render_template("error.html")

"""
Home Page
"""
@app.route('/')
def home():
    enabled = session.get("enabled", default_beacons)
    arbitrary = session.get("arbitrary", [])
    return render_template("index.html", enabled=enabled, arbs=arbitrary)

@app.route("/send_command", methods=["GET"])
def send_command():
    if request.method == "GET":
        incoming = request.args.get("command")
        serial_dev.serial_write(incoming + "\n")

    return redirect('/')

"""
Sniffer Page 
"""
sniffer_mode = False

@app.route("/sniffer", methods=["GET"])
def sniffer():
    global sniffer_mode

    if request.method == "GET":
        action = request.args.get("action")
        if action == "enable":
            serial_dev.serial_write("sniffer start\n")
            sniffer_mode = True
        elif action == "disable":
            serial_dev.serial_write("sniffer stop\n")
            sniffer_mode = False

    return render_template("sniffer.html", sniffer_mode=sniffer_mode)

"""
Sniffer Data API

When a GET request is recieved, returns all ibeacons pre-rendered as a table 
in html. Ordered by MAC address
"""
beacons = []

@app.route("/sniffer_get", methods=["GET"])
def sniffer_get():
    global beacons

    if request.method == "GET":
        while not serial_dev.is_queue_empty(Queues.SNIFFER):
            beacon = serial_dev.get_next_queue_item(Queues.SNIFFER)

            for idx, beac in enumerate(beacons):
                if beac["address"] == beacon["address"]:
                    beacons[idx] = beacon
                    break
            else:
                beacons.append(beacon)
    
    beacons.sort(key=lambda x: x["address"])
    return render_template("sniffer_table.html", beacons=beacons)

"""
Update Beacon List
When a POST is recieved, compares 
"""
@app.route("/update_beacons", methods=["POST", "GET"])
def update_beacons():
    alert = ""
    
    if request.method == "GET":
        incoming = set(request.args.getlist("enabled"))
    else:
        incoming = set(request.form.getlist("enabled"))
    current = set(session.get("enabled", default_beacons))

    beacons_to_add = incoming - current
    beacons_to_remove = current - incoming

    if len(incoming) >= 4:
        session["enabled"] = list(incoming)

        for beacon in beacons_to_add:
            command = f"ibeacon add {beacon[5]}\n"
            serial_dev.serial_write(command)
        for beacon in beacons_to_remove:
            command = f"ibeacon remove {beacon[5]}\n"
            serial_dev.serial_write(command)
    else:
        flash("Must have at least 4 beacons on", "Error")

    return redirect('/')

@app.route("/update_from_dev", methods=["GET"])
def update_from_dev():
    serial_dev.serial_write("ibeacon list\n")
    time.sleep(0.2)

    enabled_beacons = []
    while not serial_dev.is_queue_empty(Queues.IBEACONS):
        beacon = serial_dev.get_next_queue_item(Queues.IBEACONS)["name"]
        enabled_beacons.append(beacon) 

    arbitrary_beacons = []
    while not serial_dev.is_queue_empty(Queues.ARBITRARY):
        beacon = serial_dev.get_next_queue_item(Queues.ARBITRARY)
        arbitrary_beacons.append(beacon) 

    session["arbitrary"] = arbitrary_beacons

    return redirect(url_for('update_beacons', enabled=enabled_beacons))

@app.route("/get_position", methods=["GET"])
def get_position():
    unfiltered_position = serial_dev.get_next_queue_item(Queues.UNFILTERED)
    kalman_position = serial_dev.get_next_queue_item(Queues.KALMAN)
    logs = []
    while not serial_dev.is_queue_empty(Queues.LOG):
        logs.append(serial_dev.get_next_queue_item(Queues.LOG))

    return jsonify([unfiltered_position, kalman_position, logs])

@app.route("/rage")
def rage():
    return render_template("rage.html")

if __name__ == '__main__':
    serial_dev.serial_start()
    app.run()
