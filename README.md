# Making Waves — Magenta Mimas

Interactive historical storytelling installation for the Queensland Maritime Museum. A ship's helm fitted with IMU sensors connects over BLE to a central base node. The base node fuses sensor data with an Unscented Kalman Filter, emits HID keyboard events over USB, and streams telemetry to a host PC. A Unity scene renders the projected ocean display and responds to the HID input in real time.

---

## System Architecture

```
┌─────────────┐        BLE (NUS)        ┌─────────────┐
│  helm_a     │ ──────────────────────► │             │   USB-HID   ┌──────────┐
│  (node 0)   │                         │    base     │ ──────────► │  Unity   │
└─────────────┘                         │  (central)  │             │  (PC)    │
                                        │             │   USB-CDC   ┌──────────┐
┌─────────────┐        BLE (NUS)        │             │ ──────────► │  coms.py │
│  helm_b     │ ──────────────────────► │             │             │  (PC)    │
│  (node 1)   │                         └─────────────┘             └──────────┘
└─────────────┘                                                           │
                                                                    InfluxDB Cloud
```

### Data Flow

1. Each helm node samples the **LSM6DSL IMU** at 416 Hz (interrupt-driven) and reads the battery ADC on a timer.
2. Sensor packets are CRC-validated and sent to the base over **Bluetooth LE NUS** (Nordic UART Service).
3. The base runs an **Unscented Kalman Filter** fusing accelerometer and gyroscope readings from both nodes, producing a magnitude and direction of helm rotation.
4. The filtered result is translated into **HID keyboard codes** (see table below) and sent to Unity over USB.
5. Every 5 seconds the base serialises a **JSON status packet** (connection, IMU, battery, speed, direction) over serial UART to the PC script.
6. `coms.py` displays the live log and forwards telemetry points to **InfluxDB Cloud**.

---

## Repo Structure

```
root/
├── base/                    # Central BLE node firmware (Zephyr)
│   └── src/
|       ├── main.c           #   Entry point, watchdog loop, handles ble pulse timing
│       ├── gatt.c/h         #   BLE central — scan, connect, NUS subscribe
│       ├── translation.c/h  #   UKF → HID + JSON serialisation thread
│       ├── ukf.h            #   Unscented Kalman Filter state
│       ├── rb_tree.c        #   Red-black tree for per-node state
│       ├── json.c/h         #   JSON encode/decode helpers
|       ├── matrix.c/h       #   Helper functions for matrix math
|       ├── uart.c/h         #   IQR driven UART
|       ├── usb_hid.c/h      #   Handles HID emulation
│       └── usbd_init.c/h    #   USB device stack initialisation
├── helm/                    # Peripheral helm node firmware (Zephyr)
│   └── src/
│       ├── main.c           #   Entry point, watchdog loop
│       ├── gatt.c/h         #   BLE peripheral — NUS advertise & send
│       ├── imu.c/h          #   LSM6DSL driver, PM suspend/resume
│       ├── battery.c/h      #   ADC battery voltage & charge
│       └── sensors.c        #   Sensor aggregation thread
├── drivers/                 # Out-of-tree Zephyr driver module
│   └── sensor/
│       └── xiao_battery/    #   XIAO BLE battery ADC sensor driver
├── dts/                     # Custom DTS bindings (battery ADC)
├── include/
│   ├── common.h             # Shared packet structs, timestamp helpers
│   └── mac.h                # Hardcoded BLE MAC addresses (whitelist)
├── pc/
│   ├── coms.py              # PyQt5 GUI — serial bridge & InfluxDB writer
│   └── requirements.txt     # Python dependencies
├── MakingWavesVideoPlayer/  # Unity project
├── Justfile                 # Build/flash recipes
└── west.yml                 # West manifest (csse4011-sdk)
```

---

## Hardware

| Component | Part | Notes |
|-----------|------|-------|
| Helm nodes × 2 | Seeed XIAO BLE (nRF52840 Sense) | Built-in LSM6DSL IMU + LiPo charger |
| Base node × 1 | Seeed XIAO BLE (nRF52840 Sense) | Acts as BLE central + USB HID/CDC device |
| Host PC | Any | Runs Unity and `coms.py` |

The two helm nodes are differentiated at compile time by a numeric **`ID`** flag (0 or 1), which selects the corresponding pre-configured static BLE MAC address from `include/mac.h`.

---

## HID Key Mapping

The base translates Kalman-filtered helm angular velocity into keyboard keys that Unity reads via the Input System:

| Key | Direction | Relative Speed |
|-----|-----------|---------------|
| `A` | CCW | ×8 (fastest) |
| `S` | CCW | ×4 |
| `D` | CCW | ×2 |
| `F` | CCW | ×1 (slowest) |
| `H` | CW  | ×1 (slowest) |
| `J` | CW  | ×2 |
| `K` | CW  | ×4 |
| `L` | CW  | ×8 (fastest) |

Unity lerps the camera's yaw toward the target velocity each frame, giving smooth inertial panning.

---

## Building & Flashing

Requires [west](https://docs.zephyrproject.org/latest/develop/west/index.html) and [just](https://github.com/casey/just).

```bash
# Initialise the west workspace (first time only)
west init -l .
west update

# Build a target — ID selects node 0 or 1
just build helm 0     # helm node A
just build helm 1     # helm node B
just build base 0     # base node (ID ignored but required)

# Build and flash via UF2 bootloader
just all helm 0
just all helm 1
just all base 0
```

Default board is `xiao_ble/nrf52840/sense`. To use a different board, edit the `default_board` variable in the [Justfile](Justfile).

---

## PC Script Setup

Requires Python 3.10+.

```bash
cd pc
pip install -r requirements.txt
python coms.py
```

1. Connect the base node over USB.
2. Select the serial port from the drop-down and click **Connect**.
3. Use **Update Sampling Time** to change the IMU poll interval (50–1000 ms).
4. Click **Sync Timestamps** to push the current Unix timestamp to the base, enabling absolute time on telemetry points.

Telemetry (connection status, battery voltage/charge, helm velocity) is written to an **InfluxDB Cloud** instance in real time. The bucket and credentials are configured at the top of `coms.py`.

---

## Unity Setup

Open `MakingWavesVideoPlayer/` in Unity 2022.3+ (URP). The main scene is `Assets/OceanScene.unity`.

- Video files should be placed in `Assets/StreamingAssets/`.
- The `PlayerRotateWithInertia` script reads keyboard input; no additional configuration is needed once the base node is connected as a HID device.
- Press **Escape** (`QuitProgram`) to exit the application cleanly.

---

## Authors

Jack Cairns, Eden Mehr, Muhammed Abdilrahmin — CSSE4011, 2026.  
Licensed under the [Apache-2.0 License](https://www.apache.org/licenses/LICENSE-2.0).
