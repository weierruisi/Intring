<div align="center">

<h1>Intring</h1>

<div>
<a href="https://github.com/weierruisi/Intring/stargazers" target="_blank"><img src="https://img.shields.io/github/stars/weierruisi/Intring" /></a>
<a href="https://github.com/weierruisi/Intring/forks" target="_blank"><img src="https://img.shields.io/github/forks/weierruisi/Intring" /></a>
<img src="https://img.shields.io/badge/ESP--IDF-v5.3.1-blue?style=flat&labelColor=555555" />
<a href="./LICENSE"><img src="https://img.shields.io/badge/license-Apache--2.0-e8753f?style=flat&labelColor=555555" /></a>
</div>

<a href="./README.md">简体中文</a> | English

</div>

---

💡 **Intring** is an `ESP32-C3` smart interactive ring project with trackball input, air-mouse control, gesture recognition, BLE HID, low-power sleep, and BLE OTA upgrades.

- 📺 Demo Video: [Bilibili](https://www.bilibili.com/video/BV1hXLR6JEks/)
- 🧩 Hardware Package: [Intring V1](https://www.jlc-ycs.com/platform/detail/f7513283168043cfa72ec83cd0e0180a) / [Charging Case](https://www.jlc-ycs.com/platform/detail/a2e6c5c35f034d558feaef7857bcfce5?type=1)
- 🚀 Use Cases: mini mouse, motion remote, presentation control, mobile shortcuts
- 👓 Applicable Devices: AR/VR/MR glasses, tablets, PCs, etc.

## 📷 Device Photos

| Charging Case & Ring | Wearing |
| --- | --- |
| ![Intring device photo (front)](./pics/pic2.jpg) | ![Intring device photo (wearing)](./pics/pic3.jpg) |

> [!NOTE]
> This README focuses on practical usage: what each key does, how modes switch, and how to use tools quickly.

## 💡 Project Overview

| Item | Value |
| --- | --- |
| 📺 Demo Video | [Bilibili](https://www.bilibili.com/video/BV1hXLR6JEks/) |
| 🧩 Hardware Download | [Intring V1](https://www.jlc-ycs.com/platform/detail/f7513283168043cfa72ec83cd0e0180a)<br>[Charging Case](https://www.jlc-ycs.com/platform/detail/a2e6c5c35f034d558feaef7857bcfce5?type=1) |
| 🔧 MCU | `ESP32-C3` |
| 📡 Main Protocols | `BLE HID` / `BLE OTA` |
| 🛠️ Target IDF | `ESP-IDF v5.3.1` |
| 🔄 OTA Partition Strategy | `ota_0` / `ota_1` |

## 🧭 Quick Navigation

| Entry | Description |
| --- | --- |
| 🧱 [System Block Diagram](#system-block-diagram) | A brief description of the project's functional implementation |
| 🚀 [Usage Guide](#usage-guide) | First flash, BLE OTA upgrade, and air-mouse calibration |
| 🔌 [Pin Configuration and Defaults](#pin-configuration-and-defaults) | First stop for wiring/remap work |
| 🧪 [Feature Guide (Detailed)](#feature-guide-detailed) | Full behavior of keys/modes/gestures |
| 📁 [Project Structure](#project-structure) | Codebase layout |
| 🧰 [tools Usage Guide](#tools-usage-guide) | OTA/data/training scripts |
| 📜 [License](#license) | License boundary between software and hardware materials |

---

<a id="usage-guide"></a>

## 🚀 Usage Guide

### 1. Serial Flashing (Required for First Flash)

The first firmware installation must be done over the serial port. BLE OTA can be used for later updates after the initial flash is complete.

1. Connect the FPC main board to the downloader, then connect the downloader to your computer.
2. Manually enter download mode: hold `BOOT` -> press `RST` -> release `RST` -> release `BOOT`.
3. Build the project and use `esptool.py` to write the firmware to the `ESP32-C3`.

```bash
idf.py build
python -m esptool --chip esp32c3 --port <PORT> --baud 460800 write_flash @build/flash_args
```

> [!TIP]
> Replace `<PORT>` with your local serial port, such as `COM3` on Windows or `/dev/ttyUSB0` on Linux/macOS.

### 2. BLE OTA Wireless Upgrade (Recommended for Later Updates)

After the first serial flash, BLE OTA is recommended for later firmware updates.

1. Record the `ESP32-C3` BLE MAC address in advance.
2. Clone or pull this repository locally, then build the `.bin` file to be updated.
3. Run `tools/ota/ble_ota_upload.py` with the target MAC address and the `.bin` file to push the firmware wirelessly.

```bash
python tools/ota/ble_ota_upload.py --address xx:xx:xx:xx:xx:xx --bin build/Intring.bin
```

### 3. First-Time Air-Mouse Calibration

Before using air-mouse mode for the first time, perform one calibration.

1. Switch Intring to air-mouse mode.
2. Place Intring flat on a desk and keep it completely still.
3. Long-press `A + B` together, then release the keys when the LED status indicator starts blinking.
4. After release, Intring will calibrate automatically. Keep it still during the calibration process.
5. When calibration is complete, the LED will fast-blink. After Bluetooth is connected, the LED will stay steady ON.

> [!IMPORTANT]
> Do not move or shake Intring during calibration, otherwise the air-mouse attitude baseline may be inaccurate.

---

<a id="system-block-diagram"></a>

## 🧱 System Block Diagram

![System Block Diagram](./pics/pic1.png)

---

<a id="pin-configuration-and-defaults"></a>

## 🔌 Pin Configuration and Defaults

### 🛠️ Configuration Method

Pin mapping is configured through `CONFIG_GPIO_*` entries in `sdkconfig`.
Use `menuconfig` instead of manual file editing.

- Entry: `idf.py menuconfig`
- Path: `inter-ring gpios config`
  - `key gpios config`
  - `trackball direction gpios config`
  - `LED&PM gpio config`
  - `i2c_lsm6ds3tr`

> [!TIP]
> Rebuild and re-flash after pin changes.

### 📍 Default Pins (current sdkconfig)

| Function | Macro | Default GPIO |
| --- | --- | --- |
| Trackball press key | `CONFIG_GPIO_BALLKEY` | `GPIO8` |
| Touch key A | `CONFIG_GPIO_TOUCHKEY_A` | `GPIO4` |
| Touch key B | `CONFIG_GPIO_TOUCHKEY_B` | `GPIO2` |
| Trackball forward | `CONFIG_GPIO_FORWARD` | `GPIO6` |
| Trackball back | `CONFIG_GPIO_BACK` | `GPIO3` |
| Trackball right | `CONFIG_GPIO_RIGHT` | `GPIO5` |
| Trackball left | `CONFIG_GPIO_LEFT` | `GPIO7` |
| LED / power indicator | `CONFIG_GPIO_LED_PM_NUM` | `GPIO10` |
| IMU I2C SDA | `CONFIG_GPIO_I2C_SDA` | `GPIO18` |
| IMU I2C SCL | `CONFIG_GPIO_I2C_SCK` | `GPIO19` |

### ⚠️ Pin Remap Notes

- Touch keys and trackball inputs are pull-up active-low; keep electrical logic consistent.
- If IMU I2C pins are remapped, verify pull-ups and bus stability.
- If LED pin is remapped, ensure the selected GPIO supports the active LED path (LEDC / GPIO).

---

<a id="feature-guide-detailed"></a>

## 🧪 Feature Guide (Detailed)

### 1. 🧠 System States and Modes

| State | Values | Description |
| --- | --- | --- |
| System Mode | `Windows` / `Android` | Controls key + gesture mapping |
| Work Mode | `Trackball` / `Air Mouse` | Controls cursor source |
| Gesture Switch | `ON` / `OFF` | Controls gesture result consumption |

### 2. 💻 System Mode Switch (Win/Android)

| Trigger | Key Event | Behavior |
| --- | --- | --- |
| A + B + BALL pressed together | `KEY_TOUCH_A_B_BALL` | Toggle Win/Android, save NVS, reboot |

### 3. 🖱️ Work Mode Switch (Trackball/Air Mouse)

| Trigger | Key Event | Behavior |
| --- | --- | --- |
| Double-click A | `KEY_TOUCH_A_DOUBLE_CLICK` | Toggle Trackball/Air Mouse |

<details>
<summary>Switch internals (click to expand)</summary>

- To Air Mouse: enable IMU, clear mouse queue, reset air mouse filter state, suspend trackball task.
- To Trackball: reset trackball state, resume trackball task; stop IMU timer if gesture is OFF.

</details>

### 4. 🪄 Gesture Recognition ON/OFF

| Trigger | Key Event | Behavior |
| --- | --- | --- |
| Double-click B | `KEY_TOUCH_B_DOUBLE_CLICK` | Toggle Gesture ON/OFF |

<details>
<summary>Enable/disable internals (click to expand)</summary>

When ON:
- set `HID_DEV_AIR_GESTURE`
- clear stale gesture queue
- start IMU + timer when not in air mouse mode
- discard first 1 second to reduce false triggers
- resume gesture task

When OFF:
- clear `HID_DEV_AIR_GESTURE`
- suspend gesture task and clear IMU buffer
- stop IMU + timer when not in air mouse mode

</details>

### 5. 🔄 A+B Long Press Branch Logic

| Trigger | Key Event | Branch Behavior |
| --- | --- | --- |
| A + B long press | `KEY_TOUCH_A_B_LPRESS` | Non-air-mouse: reboot; air-mouse: release after LED blink, then calibrate and save NVS |

### 6. ⌨️ Full Key Event Table (Standard Firmware)

> [!IMPORTANT]
> Based on standard firmware behavior (`CONFIG_COLLECT_DATA_EN = 0`).

| Event | Trigger | Behavior |
| --- | --- | --- |
| Single click | BALL | Mouse left down; release on key release |
| Single click | A | Wheel up step (Android uses multi-step ramp) |
| Single click | B | Wheel down step (Android uses multi-step ramp) |
| Long press | A | Same class as A single-click (with cadence delay) |
| Long press | B | Same class as B single-click (with cadence delay) |
| Double click | A | Toggle Trackball / Air Mouse |
| Double click | B | Toggle Gesture ON/OFF |
| Long press | A + B | Trigger air-mouse calibration in air-mouse mode, or direct reboot outside air-mouse mode |
| Combo | A + B + BALL | Switch Win/Android + reboot |

### 7. 🧭 Gesture Mapping

#### 🪟 Windows Mode

| Gesture | Action |
| --- | --- |
| LEFT | Volume Down |
| RIGHT | Volume Up |
| UP | PageDown |
| DOWN | PageUp |
| O | PrintScreen |
| X | Alt + F4 |
| D | Win + D |

#### 🤖 Android Mode

| Gesture | Action |
| --- | --- |
| LEFT | Previous Track |
| RIGHT | Next Track |
| UP | Volume Up |
| DOWN | Volume Down |
| O | PrintScreen |
| X | Power |
| D | Win + H |

> [!TIP]
> Volume gestures use step acceleration and reset after timeout.

### 8. 🕹️ Runtime Behavior (Trackball / Air Mouse)

- Trackball mode: edge-direction input + reverse brake + streak acceleration + diagonal normalization + residue compensation.
- Air mouse mode: IMU-to-cursor conversion + dynamic threshold + filtering for smoothness and anti-jitter.

### 9. 💡 LED and Link Behavior

| Scenario | LED Behavior |
| --- | --- |
| Not connected | Fast blink |
| Connected | Mode indication blink then steady ON (Win 2 / Android 4) |
| OTA transfer running | Breathing LED |

### 10. 📊 Data-Collection Firmware Difference

When `CONFIG_COLLECT_DATA_EN=1`:
- B key behavior is repurposed for dataset queue flow.
- Double-click branches differ from standard user firmware.
- Recommended only for training-data workflows.

---

<a id="project-structure"></a>

## 📁 Project Structure

```text
Intring/
├─ components/
│  ├─ air_mouse/
│  ├─ ble_hid/
│  ├─ ble_ota/
│  ├─ gesture_detect/
│  ├─ key/
│  ├─ light_sleep/
│  └─ LSM6DS3TR/
├─ main/
├─ managed_components/
├─ tools/
│  ├─ ota/
│  └─ nn/
├─ partitions.csv
├─ sdkconfig
├─ README.md
└─ README_EN.md
```

## 🧩 Component Overview

| Component | Responsibility |
| --- | --- |
| `components/ble_hid` | BLE HID, key mapping, mode switching, LED state machine |
| `components/ble_ota` | OTA control flow, partition write, reboot switch |
| `components/air_mouse` | Air-mouse motion conversion, dynamic threshold/filter |
| `components/gesture_detect` | Gesture inference and action mapping |
| `components/key` | Key scan, debounce, combo-event generation |
| `components/light_sleep` | Sleep countdown and wake-source management |
| `components/LSM6DS3TR` | IMU driver and data acquisition |

---

<a id="tools-usage-guide"></a>

## 🧰 tools Usage Guide

### 📡 OTA Uploader

- Script: `tools/ota/ble_ota_upload.py`
- Purpose: BLE OTA firmware upload.

Common args:
- `--address`: device MAC (required)
- `--bin`: firmware path (required)
- `--chunk-size`: chunk size (default `244`)
- `--delay-ms`: inter-chunk delay (default `1`)
- `--no-response`: write without response (faster, less reliable)

```powershell
python tools/ota/ble_ota_upload.py --address xx:xx:xx:xx:xx:xx --bin build/Intring.bin
```

Example output:

```text
[INFO][0 min 0 s] Firmware: build/Intring.bin
[INFO][0 min 0 s] Size: 980208 bytes
[INFO][0 min 0 s] Address: xx:xx:xx:xx:xx:xx
[INFO][0 min 0 s] CTRL UUID: 0000fff1-0000-1000-8000-00805f9b34fb
[INFO][0 min 0 s] DATA UUID: 0000fff2-0000-1000-8000-00805f9b34fb
[INFO][0 min 0 s] Chunk size: 244
[INFO][0 min 0 s] Data write response: True
[INFO][0 min 12 s] Connected
[INFO][0 min 12 s] CTRL notify enabled
[INFO][0 min 12 s] Send START (0x01)
[NOTIFY] CTRL status: xx
[INFO][0 min 0 s] Progress: 4880/980208 (0.50%), Speed: 5.36 kB/s, Avg: 5.36 kB/s, ETA: 2 min 57 s
...
[INFO][2 min 48 s] Send FINISH (0x02)
```

### 🔌 Serial Data Collector

- Script: `tools/nn/Serial_read.py`
- Purpose: collect IMU data over serial into `tools/nn/TraningData/`

```powershell
python tools/nn/Serial_read.py
```

### 📶 BLE Data Collection Example

- Script: `tools/nn/collect_data.py`
- Purpose: collect BLE notify data into CSV (default `tools/nn/dataset/`)

```powershell
python tools/nn/collect_data.py --label wave --duration 2.0
```

### 🧠 Model Training and Export

- Script: `tools/nn/train_model.py`
- Purpose: train and export `gesture_model_quant.tflite` + `gesture_model.cc`

```powershell
python tools/nn/train_model.py
```

---

## 🧷 Version and Compatibility

- Target MCU: `ESP32-C3`
- ESP-IDF: `v5.3.1`
- Partition strategy: dual OTA partitions (`ota_0` / `ota_1`)

## 🛡️ Maintenance Notes

- Verify target MAC before OTA.
- Keep enough battery before OTA to avoid interruption.
- If gesture model is replaced, sync `components/gesture_detect/gesture_model.cc` and rebuild.

<a id="license"></a>

## 📜 License

Firmware and software source code in this repository are licensed under the [Apache License 2.0](./LICENSE).

> [!WARNING]
> Hardware design files are not included in this repository, including schematics, PCB files, Gerber files, BOM, and enclosure/mechanical files. Hardware materials are provided separately on 硬创社, and their allowed usage should follow the terms stated on that platform.
> Hardware package links: [Intring V1](https://www.jlc-ycs.com/platform/detail/f7513283168043cfa72ec83cd0e0180a) / [Charging Case](https://www.jlc-ycs.com/platform/detail/a2e6c5c35f034d558feaef7857bcfce5?type=1).
