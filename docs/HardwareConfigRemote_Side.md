# 📡 Hardware Documentation: ESP-01 "Fire-and-Forget" Doorbell Remote

**Document Version:** 1.0  
**Target Hardware:** ESP-01 / ESP-01S (ESP8266)  
**Architecture:** VCC-Switched True 0µA Standby  

---

## 1. Architecture Overview

Unlike traditional battery-powered IoT devices that rely on the ESP8266's "Deep Sleep" mode (which still draws ~20µA and requires complex GPIO16/RST wiring), this remote utilizes a **VCC-Switched "Fire-and-Forget" topology**. 

By placing the physical push-button directly on the main VCC power line, the device draws **literal 0.00µA** when idle. When pressed, the ESP-01 boots, immediately increments and saves its rolling security counter to EEPROM, fires the encrypted ESP-NOW RF packets, and gracefully dies when the button is released and the hold-up capacitor drains. 

This eliminates battery drain, deep-sleep bugs, and the need for complex power management ICs.

---

## 2. Bill of Materials (BOM)

| Qty | Component | Specification / Notes |
| :--- | :--- | :--- |
| 1 | **Microcontroller** | ESP-01 or ESP-01S (ESP8266) |
| 1 | **Push Button** | Momentary tactile switch (Doorbell trigger) |
| 1 | **Hold-Up Capacitor** | **1000µF to 2200µF** Low-ESR Electrolytic (Rated ≥6.3V)<br>*OR* **0.1F (100,000µF) 5.5V Supercapacitor** + **22Ω** series resistor |
| 3 | **Pull-up Resistors** | 10kΩ (for CH_PD, RST, GPIO0) |
| 1 | **Current Limiter** | 330Ω (for Status LED) |
| 1 | **Status LED** | 3mm or 5mm Standard LED (Any color) |
| 1 | **Power Source** | 2x AA/AAA (3.0V), CR123A (3.0V), or LiPo (3.7V) |

---

## 3. Wiring Diagram & Pinout

### 📌 ESP-01 Pin Mapping

| Pin # | Pin Name | Connection Target | Description |
| :---: | :--- | :--- | :--- |
| **1** | **GND** | Battery (-), Cap (-) | System Ground |
| **2** | **TXD** | *No Connection (NC)* | Leave floating |
| **3** | **GPIO2** | 330Ω → LED Cathode (-) | Status LED (Active LOW) |
| **4** | **CH_PD** | 10kΩ → VCC | Chip Enable (Must be HIGH to boot) |
| **5** | **GPIO0** | 10kΩ → VCC | Boot Mode Select (HIGH = Run Mode) |
| **6** | **RST** | 10kΩ → VCC | Reset (Must be HIGH to prevent reboot loops) |
| **7** | **RXD** | *No Connection (NC)* | Leave floating |
| **8** | **VCC** | Button, Cap (+), Pull-ups | Main Power Rail (2.8V - 3.6V) |

### 🎨 Schematic Logic

```text
                               [ BATTERY (+) ]
                                     |
                              [ PUSH BUTTON ]
                                     |
         +---------------------------+---------------------------+
         |                           |                           |
      [ CAP ]                     [10kΩ] x3                  [LED Anode]
    (Hold-up)                 (to CH_PD, RST, GPIO0)             |
         |                           |                        [LED Cathode]
         |       +-------------------+-------------------+       |
         |       |                   |                   |       |
         |     Pin 8 (VCC)         Pin 4 (CH_PD)      Pin 3 (GPIO2)
         |       |                   |                   |       |
         |       |    +-------------------------+        |      [330Ω]
         |       +----|       ESP-01 / 01S      |--------+       |
         |            |                         |                |
         |       +----|                         |--------+       |
         |       |    +-------------------------+        |       |
         |     Pin 1 (GND)             Pin 5 (GPIO0)   Pin 6 (RST)
         |       |                       |               |       |
         +-------+-----------------------+---------------+-------+
                                     |
                               [ BATTERY (-) ]