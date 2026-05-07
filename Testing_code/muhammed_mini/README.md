# Muhammed Abdilrahmin and Jack Boyd's iBeacon Range Finder

Simply, this is a system that allows for the communication of two devices 
(base and mobile) - the mobile node transmits RSSI information from beacons to
a base node and the base node attempts to laterate it via the least squares
algorithm, and a tuned Kalman filter for smoothing.

The base node implements functionalities such as allowing for the filtering of
the 13 nodes - as well as allowing for a list of other user-defined nodes. These
are not used for lateration.

The base node can act as a sniffer node and the data displayed in a table in the
provided Python file. Speaking of which:

## Installation
Should be able to run on both windows and Linux. 
Just need to change port from "/dev/ttyACM.." to "COM..."

## Usage
Make sure to first install all requirements with pip. Then run the server with:

```python
python server.py
```
Once run you can access website on:
```
http://127.0.0.1:5000
```

The server will output the current position/sniffer data/logs/beacons on the
given locally hosted website - moreover, it also posts Velocity and Distance
data to an InfluxDB database which also has an included dashboard.

### References:

#### Muhammed:
- Zephyr documentation pages for various functionalities 
    (k\_heap, k\_mutex, slist\_t, etc.)
- Zephyr Online kconfig listing (this is essentially the same thing as 
        menuconfig)
- using grep with zephyr/samples/* to look for functions and usage 
                                   (specifically for peripheral bluetooth)
- Sam Kwort's Bluetooth NUS central-side code
- Various ZephyrRTOS C library files (e.g. zephyr/subsys/blueooth/host/conn.c 
    to see how SYS\_SLIST\_PEEK\_NEXT\_CONTAINER works)
- Nordic DevZone for help with some of the bluetooth functions - specifically 
    usage of bt\_data\_parse
- My code from previous practicals (specifically practical 5)
- MDN (Mozilla Developer Network) helped significantly with some JS code that 
    I had to write - dealing with string literals, JS-specific syntax, etc.
    Also, it was an invaluable resource for CSS (e.g. what a display: flex is)
- InfluxDB's Python Client Deep Dive - interfacing help:
    https://www.influxdata.com/blog/influxdb-python-client-library-deep-dive-writeapi/
- When working on Jack's viewer code, his current code. I used his syntax and
    the way he dealt with Flask/Django as a reference on how to interface with
    their API functions.
- https://www.matrixmultiplicationscalculator.com/ was a very useful source for
    simplifying the Kalman matrices, and
- https://wirelesspi.com/the-easiest-tutorial-on-kalman-filter/ really is the
    easiest tutorial on the Kalman filter!
