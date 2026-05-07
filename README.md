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

---

## Dev Notes (Eden / Muhammed)

### `.clang-format` / clangd

The `.clangd` config at the root sets `CompilationDatabase: .`, meaning clangd looks for `compile_commands.json` in the repo root.

The `just build` recipe automatically:
1. Strips a few GCC flags that clangd doesn't understand (`-fno-reorder-functions`, `-fno-printf-return-value`, `-mfp16-format=ieee`) from the generated `compile_commands.json`.
2. Updates a `compile_commands.json` symlink in the repo root pointing at the most recently built target's compile commands.

This means **after building any target, clangd will use that target's compile commands** for the whole repo. If you're switching between `base` and `helm` and getting wrong includes/errors in your editor, just run `just build <target>` for whichever one you're actively working on.
