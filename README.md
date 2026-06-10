# RemoteDoorBell

A **wireless doorbell system** using ESP8266 microcontrollers (NodeMCU/ESP-01) with **AES-128 encrypted** ESP-NOW communication.

## ⭐ Highlights

- **🚀 ESP-NOW Protocol**: Lightweight, mesh-capable wireless communication—no WiFi router needed!
- **💡 Optimized for Tiny Hardware**: Fully functional on **ESP-01 & ESP8266 with only 512KB flash**
- **🔧 Customized Implementation**: Tailored ESP-NOW stack for resource-constrained devices
- **🛡️ Secure Communication**: AES-128 ECB encryption + rolling counter replay protection

## Features

- 🔐 **Secure**: AES-128 ECB encryption for all messages
- 📡 **Wireless**: **ESP-NOW protocol** (no WiFi network required)
- 🔘 **Multi-remote**: Support up to 8 paired remote transmitters
- 🔧 **Flexible output**: Relay, buzzer (ding-dong), or simple buzzer modes
- 🪫 **Low power**: Remote sleeps at ~20µA, wakes on button press
- 💾 **Persistent**: Stores paired remotes in EEPROM
- 🛡️ **Replay protection**: Rolling counter prevents duplicate rings
- **💾 Minimal Footprint**: Works on ESP-01 with just **512KB flash**

## System Overview

This project is split into two firmware roles:

- **Receiver** (`src/receiver_main.cpp`) — listens for encrypted ESP-NOW ring packets, stores paired remotes, validates the rolling counter, triggers a relay or buzzer, and sends an ACK back to the remote.
- **Remote** (`src/remote_main.cpp`) — boots on button press, sends a single encrypted ring packet, waits for an ACK from the receiver, and then deep sleeps to preserve battery.

### Message flow

1. Remote boots on button press and loads the counter from EEPROM.
2. Remote encrypts a 16-byte `RING` packet and sends it over ESP-NOW to the receiver.
3. Receiver decrypts the packet, verifies the packet type and sender, checks the rolling counter, triggers the selected output, then sends an `ACK` back to the remote.
4. Remote receives the `ACK` and indicates success with the LED before deep sleep.

### Important configuration notes

- `PLAINTEXT_DEBUG` must match in both `src/receiver_main.cpp` and `src/remote_main.cpp`.
- `RELAY_ACTIVE_HIGH` in `src/receiver_main.cpp` selects relay polarity when `OUTPUT_MODE` is `RELAY`.
- `receiverMac[]` in `src/remote_main.cpp` must be updated to the receiver’s MAC address after flashing the receiver.

## Hardware

### Receiver (Required)
- **ESP-12E/F** (NodeMCU v2/v3, Lolin, D1 Mini) — **Recommended** (4MB flash)
- **OR ESP-01** (512KB flash) — **Fully supported** on tiny devices ⚡

### Remote (Required)
- **ESP-01** (512KB flash) — Battery-powered transmitter 🔋

## Quick Start

1. **Flash receiver first**
   ```bash
   pio run -e receiver_12e -t upload   # NodeMCU/ESP-12E
   # or
   pio run -e receiver_01 -t upload    # Bare ESP-01 (512KB flash)
   ```

2. **Get receiver's MAC address** from serial monitor output

3. **Update remote firmware** (`src/remote_main.cpp`, line 73)
   ```cpp
   uint8_t receiverMac[] = {0x68, 0xc6, 0x3a, 0xd6, 0x59, 0x48};  // ← Replace with actual MAC
   ```

4. **Flash remote**
   ```bash
   pio run -e remote -t upload
   ```

5. **Test pairing**
   - Press pairing button on receiver (D2/GPIO0) → enters 10-second pairing window
   - Press remote button → receiver should register and ring
   - Press pairing button again to exit pairing mode

## Configuration

Edit values in firmware source files:

| Setting | File | Default | Notes |
|---------|------|---------|-------|
| Output mode | `src/receiver_main.cpp` | `BUZZER_TONE` | `RELAY`, `BUZZER_TONE`, or `BUZZER_SIMPLE`; set `RELAY_ACTIVE_HIGH` for active-HIGH relays |
| Ring duration | `src/receiver_main.cpp` | 3000 ms | How long relay/buzzer stays active |
| Pairing window | `receiver_main.cpp` line 72 | 10000 ms | Duration of pairing mode |
| Max remotes | `receiver_main.cpp` line 71 | 8 | Maximum paired transmitters |
| AES key | Both files, line ~87 | Default (insecure) | **Change before production** |
| Plaintext debug | Both files, line ~37/58 | `1` | Set to `0` for production (enables encryption) |

## Pin Mapping

### Receiver (NodeMCU/ESP-12E)
| Pin | Function |
|-----|----------|
| D5 | Output (relay/buzzer signal) |
| D2 | Pairing button (active LOW) |

### Receiver (ESP-01)
| Pin | Function |
|-----|----------|
| GPIO2 | Output (relay/buzzer signal) |
| GPIO0 | Pairing button (active LOW) |

### Remote (ESP-01)
| Pin | Function |
|-----|----------|
| RST | Button to GND (with 10K pullup to VCC) |
| GPIO2 | Status LED (active LOW, optional) |

## Build & Test

```bash
# Build all environments
pio run

# Build specific environment
pio run -e receiver_12e
pio run -e receiver_01
pio run -e remote

# Monitor serial output
pio device monitor -e receiver_12e
pio device monitor -e remote
```

## Security Notes

⚠️ **Development mode only**: The default AES key is hardcoded. For production:
1. Generate a new 16-byte random key
2. Update both `receiver_main.cpp` and `remote_main.cpp` with the same key
3. Set `PLAINTEXT_DEBUG` to `0` in both files
4. Redeploy both firmware images

## File Structure

```
src/
├── aes.h           → AES-128 header (tiny-AES-c)
├── aes.c           → AES-128 implementation
├── receiver_main.cpp   → Receiver firmware (NodeMCU/ESP-01)
└── remote_main.cpp     → Remote firmware (ESP-01)

docs/
├── USER_REVIEW.md  → Build notes & fixes
└── esp8266_ESP-01_breakout.webp → Wiring reference
```

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Remote won't respond | Verify receiver MAC in `remote_main.cpp` matches serial output |
| No sound/relay | Check output pin (D5 for NodeMCU, GPIO2 for ESP-01) |
| Pairing fails | Check pairing button wiring (active LOW, GPIO pulled up) |
| AES decryption fails | Ensure both firmware files have identical AES keys |
| Compilation error | Run `pio run --verbose` to see detailed errors |

## License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

You are free to use, modify, and distribute this project with proper attribution.
