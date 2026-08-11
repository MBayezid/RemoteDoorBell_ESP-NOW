# Updated `README.md`

Below is the complete rewritten README incorporating all reference documents. It follows the agreed structure (R1–R12), uses placeholders for missing assets, and consolidates the technical details from `remote-wiring.md`, `USER_REVIEW.md`, `ARCHITECTURE.md`, `platformio.ini`, `receiver_main.cpp`, and `remote_main.cpp`.

---

```markdown
# RemoteDoorBell_ESP-NOW

**ESP-NOW + AES-128 Encrypted Wireless Doorbell · Fire-and-Forget · Zero Standby Power**

A wireless doorbell system built from a standard wired doorbell. One ESP-01 remote,
one ESP8266 receiver, no Wi-Fi router, no cloud, no subscription. The remote draws
**0 µA** when idle — it powers on only while you hold the button, sends an
AES-128-encrypted ESP-NOW packet, and dies. The receiver validates the sender,
checks a rolling counter, and rings your existing bell.

Built as a step-by-step guide: anyone with a soldering iron and two cheap modules
can convert an old wired doorbell into a wireless one in an afternoon.

---

## Project Demo

<!-- ASSET: youtube-video -->
> 🎬 *YouTube demonstration video will be added here.*

[Watch the RemoteDoorBell_ESP-NOW demonstration](YOUTUBE_URL)

<!-- ASSET: demo-gif -->
> 📷 *6–10 s demonstration GIF will be added here.*

![RemoteDoorBell_ESP-NOW demonstration](docs/animations/demo.gif)

---

## Key Features

| # | Feature | Detail |
|---|---------|--------|
| 1 | **Zero standby power** | Button sits on the VCC line. No deep sleep, no quiescent draw. Idle current is literally 0 µA. |
| 2 | **No router required** | ESP-NOW peer-to-peer radio. Works with no Wi-Fi network, no internet, no cloud account. |
| 3 | **AES-128 encryption** | Every packet is encrypted before transmission. A sniffed capture reveals nothing. |
| 4 | **Multi-remote support** | Up to 8 remotes paired to one receiver. Pair and unpair without serial access. |
| 5 | **Replay protection** | Rolling counter + duplicate suppression + rollback auto-block. A captured packet cannot be re-sent. |
| 6 | **Fire-and-forget design** | Boot → encrypt → transmit ×3 → power off. No connection handshake, no keep-alive, no ACK wait. |
| 7 | **Works with your existing bell** | Receiver drives a relay across the original bell terminals. No new chime hardware needed. |
| 8 | **Sub-200 ms latency** | Button press to chime in under 200 ms typical. |

---

## System Architecture

<!-- ASSET: system-overview-diagram -->
> 📷 *System architecture diagram will be added here.*

The system uses two ESP8266 nodes in a **Fire-and-Forget** topology:

| Role | Board | Power | Job |
|------|-------|-------|-----|
| **Remote** | ESP-01 / ESP-01S | Battery, VCC-switched by button | Boot → increment counter → encrypt → blast ×3 → die |
| **Receiver** | NodeMCU v3 (ESP-12F) or ESP-01 | Always-on (USB / wall adapter) | Listen → decrypt → validate → ring |

No ACK handshake is used on the ring path. The remote sends three identical
packets 30 ms apart and powers off. The receiver processes the first valid
packet and suppresses duplicates. This eliminates handshake latency and allows
the remote to power down instantly.

→ Full architecture: [`docs/Architecture.md`](docs/Architecture.md)

---

## How It Works

### Remote (Fire-and-Forget)

```text
USER PRESSES BUTTON (closes VCC)
        │
        ▼
┌─────────────────────────────────────────────┐
│  ESP-01 boots                               │
│  1. Load counter from EEPROM                │
│  2. Increment counter                       │
│  3. Save counter to EEPROM  ← DONE FIRST   │
│  4. delay(100 ms) — flash write guard       │
│  5. Build 16-byte RING packet               │
│  6. AES-128 ECB encrypt                     │
│  7. ESP-NOW init, add receiver peer         │
│  8. Transmit packet × 3 (30 ms apart)       │
│  9. deepSleep(0) / power off                │
└─────────────────────────────────────────────┘
```

**Why save the counter first?** If the user releases the button too fast,
power dies mid-boot. By persisting the counter before any RF work, the next
press always sends a new value — preventing a permanent duplicate lockout.

### Receiver (Always Listening)

```text
ESP-NOW packet arrives
        │
        ▼
┌─────────────────────────────────────────────┐
│  1. Validate length (must be 16 bytes)      │
│  2. memcpy → aligned Packet struct          │
│  3. AES-128 ECB decrypt                     │
│  4. Verify type == 1 (RING)                 │
│  5. Look up sender in EEPROM whitelist      │
│  6. Rolling-counter validation:             │
│       delta == 0        → duplicate, ACK   │
│       delta > 0xF000…   → rollback, block  │
│       0 < delta ≤ 0xF0… → valid, accept    │
│  7. Update stored counter, save to EEPROM   │
│  8. Trigger relay / buzzer  🔔              │
│  9. Send ACK back to remote                 │
└─────────────────────────────────────────────┘
```

→ Full firmware documentation: [`docs/Firmware.md`](docs/Firmware.md)

---

## Hardware Overview

### Receiver BOM

| Qty | Component | Notes |
|-----|-----------|-------|
| 1 | NodeMCU v2/v3 or Wemos D1 Mini | ESP-12F, 4 MB flash, USB |
| 1 | Relay module (SRD-05VDC) **or** buzzer | Output device |
| 1 | Tactile push button | Pairing button (active LOW to GND) |
| 1 | Micro-USB cable | Power + flashing |

### Remote BOM

| Qty | Component | Specification |
|-----|-----------|---------------|
| 1 | ESP-01 or ESP-01S | ESP8266, 512 KB flash |
| 1 | Tactile push button | Momentary — wired in series with VCC |
| 1 | Hold-up capacitor | 1000 µF – 2200 µF low-ESR electrolytic (≥ 6.3 V) **OR** 0.1 F / 5.5 V supercap + 22 Ω series resistor |
| 3 | 10 kΩ resistors | Pull-ups for CH_PD, RST, GPIO0 |
| 1 | 330 Ω resistor | LED current limiter |
| 1 | LED (3 mm / 5 mm) | Boot status indicator |
| 1 | Battery | 2× AA/AAA (3.0 V), CR123A (3.0 V), or LiPo 3.7 V + HT7333/MCP1700 regulator |

> ⚠️ **The hold-up capacitor is mandatory.** It keeps the ESP-01 alive for
> ~100–200 ms after button release, guaranteeing the EEPROM write completes.
> Without it, quick taps cause counter desync.

### Current Prototype

The tested prototype uses discrete modules and point-to-point wiring on
breadboard/perfboard. No custom PCB has been manufactured.

<!-- ASSET: receiver-photo -->
> 📷 *Receiver assembly photo will be added here.*

<!-- ASSET: remote-photo -->
> 📷 *Remote assembly photo will be added here.*

→ Full hardware documentation and wiring: [`docs/Hardware.md`](docs/Hardware.md)

---

## Security

### Packet Structure (16 bytes — one AES block)

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 B | `type` | `1` = RING, `2` = ACK |
| 1 | 4 B | `sender` | ESP8266 Chip ID |
| 5 | 4 B | `counter` | Rolling counter (increments every press) |
| 9 | 7 B | `pad` | Zero-padding to fill AES block |

### Protection Layers

| Layer | Mechanism |
|-------|-----------|
| **Confidentiality** | AES-128 ECB encryption (tiny-AES-c, no SDK dependency) |
| **Authentication** | Sender Chip ID must exist in EEPROM whitelist |
| **Replay protection** | Rolling counter — `delta == 0` rejected as duplicate |
| **Rollback protection** | `delta > 0xF0000000` triggers 10 s auto-block, then re-sync |
| **Pairing gate** | Unknown senders dropped unless pairing mode is active |

### Production Checklist

- [ ] `PLAINTEXT_DEBUG` set to `0` in **both** firmware files
- [ ] AES key changed from the repository default
- [ ] Receiver serial debug may remain enabled (does not leak keys)

→ Full security analysis: [`docs/Security.md`](docs/Security.md)

---

## Performance

| Metric | Value | Conditions |
|--------|-------|------------|
| Button-to-chime latency | < 200 ms typical | Indoor, 3 m line-of-sight |
| Remote active time per press | ~150–200 ms | Boot → TX × 3 → power off |
| Remote idle current | **0 µA** | VCC physically disconnected |
| Max paired remotes | 8 | EEPROM slots |
| Ring duration | 700 ms (configurable) | Relay hold / buzzer on |

<!-- ASSET: benchmark-chart -->
> 📷 *Benchmark chart will be added here.*

→ Measurement methodology and data: [`docs/Measurements.md`](docs/Measurements.md)

---

## Repository Structure

```text
RemoteDoorBell_ESP-NOW/
├── platformio.ini              ← Build environments (3 targets)
├── README.md                   ← This file
├── src/
│   ├── remote_main.cpp         ← Remote firmware (ESP-01)
│   ├── receiver_main.cpp       ← Receiver firmware (NodeMCU / ESP-01)
│   └── aes/                    ← tiny-AES-c library (local, no SDK dep)
│       ├── aes.h
│       └── aes.c
├── docs/
│   ├── Architecture.md         ← System design, packet flow, power strategy
│   ├── Hardware.md             ← BOM, wiring, pin maps, capacitor notes
│   ├── Firmware.md             ← Build config, flash guide, configuration reference
│   ├── Security.md             ← Threat model, encryption, counter logic
│   ├── Troubleshooting.md      ← Common issues, known fixes, debug reference
│   └── Measurements.md         ← Latency, range, timing data
└── .gitignore
```

---

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB-to-serial adapter (for ESP-01) **or** micro-USB cable (NodeMCU)
- Two ESP8266 boards (one per role)

### Step 1 — Clone

```bash
git clone https://github.com/YOUR_USER/RemoteDoorBell_ESP-NOW.git
cd RemoteDoorBell_ESP-NOW
```

### Step 2 — Flash the Receiver

```bash
# NodeMCU v2/v3 / Wemos D1 Mini (recommended):
pio run -e receiver_12e -t upload

# Bare ESP-01 (512 KB fallback):
pio run -e receiver_01 -t upload
```

Open the serial monitor at **74880 baud** and note the receiver MAC:

```text
=============================
 Doorbell Receiver v1.0
 NodeMCU v3 / Lolin ESP-12F
=============================
  MAC Addr  : BC:FF:4D:34:E1:C3   ← COPY THIS
  Chip ID   : 0x1A2B3C4D
```

### Step 3 — Configure the Remote

Open `src/remote_main.cpp` and paste the receiver MAC:

```cpp
uint8_t receiverMac[] = {0xBC, 0xFF, 0x4D, 0x34, 0xE1, 0xC3};
```

### Step 4 — Flash the Remote

```bash
pio run -e remote -t upload
```

> ⚠️ **ESP-01 flashing:** GPIO0 must be LOW during upload, then HIGH to run.
> Jumper GPIO0 → GND before flashing; remove the jumper and power-cycle after.

### Step 5 — Pair

1. Power on the receiver. Wait for `[READY] Listening for remotes`.
2. Press the **pairing button** (D2 → GND) once.
3. Output blinks 3× → pairing window is open (10 s).
4. Press the remote doorbell button.
5. Serial shows: `[PAIR] New remote registered | Slot 0 | ID: 0x…`
6. Bell rings. Pairing complete.

### Step 6 — Production Hardening

In **both** `remote_main.cpp` and `receiver_main.cpp`:

```cpp
#define PLAINTEXT_DEBUG 0   // ← MUST be 0 for real deployment
```

Change the AES key to your own random 16 bytes. Re-flash both boards.

---

## Configuration Quick Reference

### Receiver (`receiver_main.cpp`)

| Define | Default | Options | Description |
|--------|---------|---------|-------------|
| `OUTPUT_MODE` | `OUTPUT_MODE_RELAY` | `1` Relay · `2` Passive buzzer (ding-dong) · `3` Active buzzer | Output type |
| `OUTPUT_PIN` | `D5` | Any GPIO | Pin driving the output |
| `PAIRING_BTN_PIN` | `D2` | Any GPIO | Pairing button (active LOW) |
| `RING_DURATION` | `700` | ms | Relay hold / buzzer on time |
| `MAX_REMOTES` | `8` | 1–8 | EEPROM whitelist slots |
| `PAIRING_WINDOW` | `10000` | ms | Pairing mode timeout |
| `ROLLBACK_BLOCK_MS` | `10000` | ms | Auto-block duration after counter rollback |
| `RELAY_ACTIVE_HIGH` | `0` | `0` or `1` | Relay trigger polarity |
| `CHANNEL` | `1` | 1–13 | ESP-NOW Wi-Fi channel |
| `PLAINTEXT_DEBUG` | `1` | `0` = encrypted, `1` = plaintext | **Must match remote** |

### Remote (`remote_main.cpp`)

| Define | Default | Description |
|--------|---------|-------------|
| `receiverMac[]` | *(must edit)* | Receiver MAC address |
| `CHANNEL` | `2` | Must match receiver |
| `TX_RETRIES` | `3` | Packet blast count |
| `PLAINTEXT_DEBUG` | `1` | Must match receiver |
| `LED_PIN` | `2` (GPIO2) | Boot status LED (active LOW) |

### ESP-01 Receiver Pin Override

When using a bare ESP-01 as receiver, add to `platformio.ini`:

```ini
build_flags =
    -DOUTPUT_PIN=2
    -DPAIRING_BTN_PIN=0
```

---

## Documentation

| Document | Contents |
|----------|----------|
| [`docs/Architecture.md`](docs/Architecture.md) | System design, Fire-and-Forget paradigm, packet flow, power strategy |
| [`docs/Hardware.md`](docs/Hardware.md) | Full BOM, wiring diagrams, pin maps, capacitor requirements |
| [`docs/Firmware.md`](docs/Firmware.md) | Build system, flash procedures, configuration reference, ESP32 porting notes |
| [`docs/Security.md`](docs/Security.md) | Threat model, AES-128, rolling counter, rollback handling |
| [`docs/Troubleshooting.md`](docs/Troubleshooting.md) | Common issues, known fixes (Exception 9, desync, boot glitch), debug output |
| [`docs/Measurements.md`](docs/Measurements.md) | Latency, range, boot timing, methodology |

---

## License

This project is open-source. See [`LICENSE`](LICENSE) for details.

---

