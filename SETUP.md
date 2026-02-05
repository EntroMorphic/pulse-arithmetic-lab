# Setup Guide

**Time required:** ~10 minutes (plus ESP-IDF download if not installed)

---

## Hardware

### Required

| Item | Purpose | Cost |
|------|---------|------|
| ESP32-C6-DevKitC-1 | The microcontroller | ~$8 |
| USB-C cable | Power and programming | ~$5 |

That's it. No breadboard, no external components, no soldering.

### Where to Buy

- [Espressif Official](https://www.espressif.com/en/products/devkits)
- [DigiKey](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-C6-DEVKITC-1-N8/17728866)
- [Mouser](https://www.mouser.com/ProductDetail/Espressif-Systems/ESP32-C6-DevKitC-1)
- Amazon, AliExpress (check for genuine Espressif)

### Verify Your Board

Look for "ESP32-C6-DevKitC-1" printed on the PCB. The chip should say "ESP32-C6" or "ESP32-C6FH4".

---

## Software

### 1. Install ESP-IDF

ESP-IDF is Espressif's development framework. Version 5.x required.

**Linux/macOS:**

```bash
# Install prerequisites (Ubuntu/Debian)
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

# Clone ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git

# Install tools
cd esp-idf
./install.sh esp32c6

# Add to shell (add this to ~/.bashrc or ~/.zshrc)
alias get_idf='. $HOME/esp/esp-idf/export.sh'
```

**Windows:**

Download and run the [ESP-IDF Windows Installer](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/windows-setup.html).

**Verify installation:**

```bash
get_idf  # Or: source ~/esp/esp-idf/export.sh
idf.py --version
# Should show: ESP-IDF v5.x
```

### 2. Clone This Repository

```bash
git clone https://github.com/EntroMorphic/pulse-arithmetic-lab.git
cd pulse-arithmetic-lab
```

### 3. Connect Your Board

1. Plug in the ESP32-C6 via USB-C
2. Find the port:

**Linux:**
```bash
ls /dev/ttyACM*
# Usually /dev/ttyACM0
```

**macOS:**
```bash
ls /dev/cu.usbmodem*
```

**Windows:**
Check Device Manager for COM port (e.g., COM3)

### 4. Build and Flash

```bash
# Activate ESP-IDF environment
get_idf  # Or: source ~/esp/esp-idf/export.sh

# Navigate to first demo
cd firmware/01_pulse_addition

# Build
idf.py build

# Flash and monitor (replace port if needed)
idf.py -p /dev/ttyACM0 flash monitor
```

**Expected output:**
```
╔═══════════════════════════════════════════════════════════════════╗
║  PULSE ADDITION: PCNT Counts Pulses = Hardware Addition           ║
╚═══════════════════════════════════════════════════════════════════╝

Test 1: Count 100 pulses
  Expected: 100
  PCNT value: 100
  PASS

...
```

Press `Ctrl+]` to exit the monitor.

---

## Troubleshooting

### "Permission denied" on Linux

```bash
sudo usermod -a -G dialout $USER
# Log out and back in
```

### "No such file or directory: /dev/ttyACM0"

- Check USB connection
- Try a different USB cable (some are charge-only)
- Try a different USB port

### Build fails with "esp32c6 not supported"

Make sure you installed ESP-IDF with C6 support:
```bash
cd ~/esp/esp-idf
./install.sh esp32c6
```

### Flash fails with "Connecting..." timeout

1. Hold the BOOT button on the board
2. Press and release the RESET button
3. Release the BOOT button
4. Try flashing again

### Monitor shows garbage characters

Baud rate mismatch. The default is 115200. Check your terminal settings.

---

## Next Steps

Setup complete! Proceed to [01_pulse_addition](firmware/01_pulse_addition/) to run your first demo.

---

## Alternative: Using VS Code

If you prefer an IDE:

1. Install [VS Code](https://code.visualstudio.com/)
2. Install the [ESP-IDF Extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)
3. Open the `firmware/01_pulse_addition` folder
4. Use the extension's build/flash buttons

The command line instructions above work regardless of IDE choice.
