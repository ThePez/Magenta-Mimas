# Making Waves — Magenta Mimas

Interactive historical storytelling installation for the Queensland Maritime Museum. A ship's helm with mounted IMU sensors connects over BLE to a central node, which drives a Unity-based projected display.

---

## Repo Structure

```
root/
├── base/           # Central BLE node firmware (Zephyr)
├── helm/           # Peripheral helm node firmware (Zephyr)
├── Testing_code/   # Scratch / test firmwares
└── museum_source/  # Unity project / GUI
```

---

## Building & Flashing

Requires [west](https://docs.zephyrproject.org/latest/develop/west/index.html) and [just](https://github.com/casey/just).

```bash
# Build a target
just build base

# Build and flash (UF2 bootloader)
just all base
```

Replace `base` with `helm`, etc. as needed. Default board is `xiao_ble/nrf52840/sense`.
