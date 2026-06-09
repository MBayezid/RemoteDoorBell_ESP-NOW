# RemoteDoorBell

A **wireless doorbell system** using ESP8266 microcontrollers (NodeMCU/ESP-01) with **AES-128 encrypted** ESP-NOW communication.

## Features

- 🔐 **Secure**: AES-128 ECB encryption for all messages
- 📡 **Wireless**: ESP-NOW protocol (no WiFi network required)
- 🔘 **Multi-remote**: Support up to 8 paired remote transmitters
- 🔧 **Flexible output**: Relay, buzzer (ding-dong), or simple buzzer modes
- 🪫 **Low power**: Remote sleeps at ~20µA, wakes on button press
- 💾 **Persistent**: Stores paired remotes in EEPROM
- 🛡️ **Replay protection**: Rolling counter prevents duplicate rings

## Hardware

### Receiver (Required)
- **ESP-12E/F** (NodeMCU v2/v3, Lolin, D1 Mini) — **Recommended** (4MB flash)
- OR **ESP-01** (512KB flash) — Fallback option

### Remote (Required)
- **ESP-01** (512KB flash) — Battery-powered transmitter

## Quick Start

1. **Flash receiver first**
   ```bash
   pio run -e receiver_12e -t upload   # NodeMCU/ESP-12E
   # or
   pio run -e receiver_01 -t upload    # Bare ESP-01
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
| Output mode | `receiver_main.cpp` line 58 | `BUZZER_TONE` | `RELAY`, `BUZZER_TONE`, or `BUZZER_SIMPLE` |
| Ring duration | `receiver_main.cpp` line 70 | 3000 ms | How long relay/buzzer stays active |
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
