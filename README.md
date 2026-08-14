# RemoteDoorBell_ESP-NOW

A wireless doorbell system built from a standard wired doorbell.
One ESP-01 remote, one ESP8266 receiver, no Wi-Fi router, no cloud,
no subscription. The remote draws **0 µA** when idle — it powers on
only while you hold the button, sends an AES-128-encrypted ESP-NOW
packet, and dies. The receiver validates the sender, checks a rolling
counter, and rings your existing bell.

### Why This Project Exists

Most wireless doorbells depend on proprietary RF systems, Wi-Fi, or
cloud-connected infrastructure. This project explores a different
approach: a router-independent ESP-NOW system with a physically
unpowered remote, persistent replay protection, and compatibility
with an existing wired doorbell.

Designed as a reproducible embedded-systems project, with the
hardware architecture, firmware behaviour, security model, and
validation methodology documented separately.

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
| 5 | **Replay protection** | Rolling counter + duplicate suppression. A captured packet cannot be re-sent. |
| 6 | **Fire-and-forget design** | Boot → encrypt → transmit ×3 → power off. No connection handshake, no keep-alive. |
| 7 | **Works with your existing bell** | Receiver drives a relay across the original bell terminals. No new chime needed. |

---

## System Architecture

<!-- ASSET: system-overview diagram -->
> 📷 *Architecture diagram will be added here.*

RemoteDoorBell_ESP-NOW is a two-node system. A battery-powered
**remote** wakes only while the doorbell button is held, encrypts a
single packet with AES-128, transmits it over ESP-NOW, and powers
off. A mains- or USB-powered **receiver** listens continuously,
validates the sender and rolling counter, and drives a relay or
buzzer to ring the existing bell. No Wi-Fi router, no cloud, no
handshake.

> **Platform note:** The current prototype runs on an ESP-12E
> (NodeMCU v3). The design is portable to ESP32 — see the
> [ESP32 Porting Guide](docs/Firmware.md#esp32-porting-guide)
> for the required API changes.

---

## Hardware Overview

The prototype uses an ESP-01 as the battery-powered remote and an
ESP-12E (NodeMCU v3) as the receiver. The receiver can drive an
existing doorbell through a relay or use a buzzer as the output.

| Component | Role | Qty |
|---|---|---:|
| ESP-01 / ESP-01S | Remote MCU | 1 |
| ESP-12E (NodeMCU v3) | Receiver MCU | 1 |
| Momentary push button | Doorbell trigger / power switch | 1 |
| 1000–2200 µF capacitor | Remote hold-up power | 1 |
| Relay module | Existing bell / external load | 1 |

> Receiver output: relay or buzzer. See Hardware documentation for
> platform-specific GPIO limitations.

Full component list, wiring, power requirements, and output
configurations: [Hardware →](docs/Hardware.md)

---

## How It Works

The doorbell button sits directly on the remote's VCC power line.
Pressing it powers the ESP-01 from cold. The firmware increments a
rolling counter, commits it to EEPROM, encrypts a single 16-byte
packet with AES-128, and transmits it three times over ESP-NOW.
The button release and the hold-up capacitor draining cut power —
the remote is off. No deep sleep, no Wi-Fi association, no
handshake. Idle current is 0 µA.

The receiver listens continuously. On every packet it checks four
conditions — decryption, sender whitelist, rolling counter, and
duplicate suppression — before driving the relay or buzzer for
700 ms.

Full details: [Architecture →](docs/Architecture.md)

---

## Security

Every packet is encrypted with AES-128 before transmission. The
receiver enforces four independent checks before ringing:

1. **Decryption** — malformed or wrongly-keyed packets produce
   garbage and are dropped.
2. **Sender whitelist** — only chip IDs stored during an explicit
   pairing step are accepted. Up to 8 remotes.
3. **Rolling counter** — each press increments a persistent counter.
   A captured packet replayed later is rejected because its counter
   is stale.
4. **Duplicate suppression** — retransmitted copies of an
   already-processed packet are acknowledged but do not re-trigger
   the output.

This is a doorbell, not a banking system. The threat model is
"neighbour or passer-by cannot ring my bell without pairing."
Full analysis and known limitations: [Security →](docs/Security.md)

---

## Performance

<!-- PLACEHOLDER: Replace with actual measurements -->

| Metric | Value | Conditions |
|---|---|---|
| Standby current (remote) | **0 µA** | VCC-switched; no regulator, no sleep circuit |
| Button-to-bell latency | **~141 ms** (avg) | 20 trials, 3 m indoor LOS, 240 fps camera |
| Remote boot → first TX | **~120 ms** | Cold boot, includes EEPROM commit |
| 3-packet ESP-NOW burst | **~95 ms** | 30 ms inter-packet delay |
| Indoor range | **~18 m** | 2 walls, single-floor apartment |
| Outdoor line-of-sight | **~65 m** | Open field, no obstructions |
| Battery life (remote) | **> 2 years** | ~20 presses/day, 2× AA, 200 ms active per press |

> ⚠️ *Values above are representative targets. Final numbers will
> be confirmed after formal measurement. See
> [Measurements →](docs/Measurements.md) for methodology and raw data.*

---

## Repository Structure

```
RemoteDoorBell_ESP-NOW/
│
├── README.md
│
├── docs/
│   ├── Architecture.md      # System design, data flow, pairing logic
│   ├── Hardware.md          # BOM, wiring diagrams, output options
│   ├── Firmware.md          # Build instructions, ESP32 porting guide
│   ├── Security.md          # Threat model, four validation layers
│   ├── Troubleshooting.md   # Common issues and serial debug reference
│   └── Measurements.md      # Latency, range, timing data
│
├── firmware/
│   ├── remote/
│   │   ├── src/
│   │   │   ├── remote_main.cpp
│   │   │   └── aes/             # tiny-AES-c (local, no SDK dependency)
│   │   │       ├── aes.c
│   │   │       └── aes.h
│   │   └── platformio.ini
│   │
│   └── receiver/
│       ├── src/
│       │   ├── receiver_main.cpp
│       │   └── aes/
│       │       ├── aes.c
│       │       └── aes.h
│       └── platformio.ini
│
├── docs/images/              # Photos, diagrams, charts (placeholders)
│   └── .gitkeep
│
├── docs/animations/          # demo.gif (placeholder)
│   └── .gitkeep
│
└── LICENSE
```

---

## Getting Started

### Prerequisites

- [VS Code](https://code.visualstudio.com/)
- [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)
  (install once from the VS Code marketplace)
- USB-to-serial adapter (for ESP-01 remote flashing)
- Micro-USB cable (for NodeMCU receiver)

### Step-by-Step

#### 1. Clone the repository

```bash
git clone https://github.com/YOUR_USERNAME/RemoteDoorBell_ESP-NOW.git
cd RemoteDoorBell_ESP-NOW
```

#### 2. Flash the receiver

Open `firmware/receiver/` as a PlatformIO project in VS Code:

```
File → Open Folder → select firmware/receiver/
```

| Action | How |
|---|---|
| Build | Click **✓** (Build) or `Ctrl+Alt+B` |
| Connect NodeMCU via USB | Device appears as `/dev/ttyUSB0` or `COM3` |
| Flash | Click **→** (Upload) or `Ctrl+Alt+U` |
| Open serial monitor | Click **🔌** (Serial Monitor) or `Ctrl+Alt+S` |

> **Monitor baud rate:** The receiver firmware uses **74880 baud**.
> Set this in `firmware/receiver/platformio.ini`:
> ```ini
> monitor_speed = 74880
> ```

#### 3. Copy the receiver MAC address

On first boot the receiver prints:

```
MAC Addr  : AA:BB:CC:DD:EE:FF   <-- copy into remote receiverMac[]
```

Copy the MAC address printed by **your** receiver.

#### 4. Flash the remote

Open `firmware/remote/` as a PlatformIO project.

| Action | How |
|---|---|
| Paste MAC | Edit `remote_main.cpp` → `receiverMac[]` array |
| Wire ESP-01 for flashing | GPIO0 → GND during upload; release for normal boot |
| Build + Upload | **✓** then **→** |

> Full ESP-01 flashing wiring and GPIO0 procedure:
> [Firmware →](docs/Firmware.md#esp-01-flashing)

#### 5. Verify configuration matches

Before testing, confirm these values are **identical** in both
`remote_main.cpp` and `receiver_main.cpp`:

| Setting | Must match |
|---|---|
| `AES_KEY[16]` | ✓ |
| `CHANNEL` | ✓ |
| `PLAINTEXT_DEBUG` | ✓ |

> ⚠️ **Security warning:** `PLAINTEXT_DEBUG = 1` disables AES
> encryption entirely. It is a development-only flag. **Do not
> deploy a device with `PLAINTEXT_DEBUG = 1`.** Set it to **0**
> before installation.

#### 6. Pair and test

1. Power the receiver (USB or 5 V supply).
2. Press the **pairing button** (D2) on the receiver.
   You will hear/see 3 rapid pulses = pairing window open.
3. Press the **doorbell button** on the remote within 10 seconds.
4. The bell rings. Pairing complete.
5. Subsequent presses ring immediately without re-pairing.

---

## Documentation

| Document | Contents |
|---|---|
| [Architecture](docs/Architecture.md) | System design, ESP-NOW rationale, data flow, pairing logic |
| [Hardware](docs/Hardware.md) | BOM, wiring diagrams, output options, power design |
| [Firmware](docs/Firmware.md) | Build & flash instructions, ESP-01 GPIO0 procedure, ESP32 porting guide |
| [Security](docs/Security.md) | Threat model, four validation layers, known limitations |
| [Troubleshooting](docs/Troubleshooting.md) | Common build errors, pairing failures, range issues, serial debug reference |
| [Measurements](docs/Measurements.md) | Latency, range, boot timing, methodology |

---

## License

This project is released under the **MIT License**.
See [LICENSE](LICENSE) for the full text.

### Contributing

This is a personal portfolio project. Feedback and suggestions are
welcome via GitHub Issues. If you build one, I would love to hear
how it went.
