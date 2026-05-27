// ============================================================
//  DOORBELL REMOTE FIRMWARE — ESP-01 (512KB)
//
//  Button wiring strategy for ESP-01:
//
//  ESP-01 has NO GPIO16, so true deep sleep wake-from-sleep
//  via GPIO is NOT possible (GPIO16→RST needed for that).
//
//  CHOSEN APPROACH: RST-wake deep sleep
//  ─────────────────────────────────────
//  Button wired between RST and GND.
//  Pressing button pulls RST low → hard reset → chip boots →
//  runs setup() → sends RING → deep sleeps again.
//
//  GPIO2 role: STATUS LED (optional)
//  ────────────────────────────────────
//  GPIO2 has a boot constraint (must be HIGH at boot).
//  We configure it as OUTPUT after boot is complete and use
//  it to drive a status LED:
//    - Fast blink during TX attempts
//    - Solid ON briefly = ACK received
//    - Off = sleeping
//
//  GPIO0 role: reserved / float
//  ────────────────────────────
//  GPIO0 LOW at boot = flash mode. Leave it floating or
//  tie HIGH via 10K resistor. Do NOT use as button.
//
//  Wiring summary:
//  ┌─────────────┬──────────────────────────────────────┐
//  │ ESP-01 Pin  │ Connection                           │
//  ├─────────────┼──────────────────────────────────────┤
//  │ VCC         │ 3.3V regulated                       │
//  │ GND         │ GND                                  │
//  │ RST         │ 10K pullup to VCC + button to GND    │
//  │ GPIO0       │ 10K pullup to VCC (float, no button) │
//  │ GPIO2       │ VCC → 330Ω → LED → GPIO2  (active LOW)│
//  │ TX / RX     │ disconnected in field (flash only)   │
//  └─────────────┴──────────────────────────────────────┘
//
//  Every button press = RST pulse = full boot cycle = RING sent.
//  Deep sleep current: ~20µA. Active time: ~250ms per press.
// ============================================================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
extern "C" {
  #include <espnow.h>
  #include "aes.h"
}

// ---- Debug mode -------------------------------------------
// PLAINTEXT_DEBUG 1 = skip AES entirely, send raw packets.
// Use this to verify the ESP-NOW channel works independently
// of encryption. Set BOTH remote and receiver to the same value.
// MUST be 0 for production deployment.
#define PLAINTEXT_DEBUG 1

// ---- Pin definitions --------------------------------------
// GPIO2: status LED (active LOW — ESP-01 onboard LED)
// LOW  = LED ON, HIGH = LED OFF
#define LED_PIN       2     // GPIO2

// ---- Configuration ----------------------------------------
#define CHANNEL          1
#define MAX_RETRIES      2
#define ACK_TIMEOUT_MS   200   // ms to wait for ACK per attempt
#define RETRY_BACKOFF_MS 50    // ms between retries

// Receiver MAC — paste the 6 bytes from receiver serial monitor here
uint8_t receiverMac[] = {0x68, 0xc6, 0x3a, 0xd6, 0x59, 0x48};

// AES-128 shared key — 16 bytes, MUST match receiver exactly
static const uint8_t AES_KEY[AES_KEYLEN] = {
  0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
  0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};

// ---- EEPROM layout ----------------------------------------
#define EEPROM_SIZE       8
#define EEPROM_COUNTER    0
#define EEPROM_MAGIC      4
#define EEPROM_MAGIC_VAL  0xDEADBEEFu

// ---- Packet structures ------------------------------------
typedef struct {
  uint8_t  type;      // 1 = RING, 2 = ACK
  uint32_t sender;    // chip ID
  uint32_t counter;   // rolling counter
  uint8_t  pad[7];    // padding to 16 bytes
} __attribute__((packed)) Packet;

typedef struct {
  uint8_t data[AES_BLOCKLEN];
} WirePacket;

typedef char _packet_size_check[(sizeof(Packet) == AES_BLOCKLEN) ? 1 : -1];

// ---- Globals ----------------------------------------------
volatile bool  ackReceived    = false;
uint32_t       messageCounter = 0;
struct AES_ctx aesCtx;

// ---- LED helpers ------------------------------------------
// GPIO2 on ESP-01 is active LOW (LOW = ON)
inline void ledOn()  { digitalWrite(LED_PIN, LOW);  }
inline void ledOff() { digitalWrite(LED_PIN, HIGH); }

void ledBlink(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    ledOn();  delay(onMs);
    ledOff(); delay(offMs);
  }
}

// ---- Forward declarations ---------------------------------
void loadCounter();
void saveCounter(uint32_t val);
void onSent(uint8_t *mac, uint8_t status);
void onReceive(uint8_t *mac, uint8_t *data, uint8_t len);

// ---- EEPROM counter persistence ---------------------------
void loadCounter() {
  EEPROM.begin(EEPROM_SIZE);
  uint32_t magic = 0;
  EEPROM.get(EEPROM_MAGIC, magic);
  if (magic == EEPROM_MAGIC_VAL) {
    EEPROM.get(EEPROM_COUNTER, messageCounter);
  } else {
    // First boot — initialise
    messageCounter = 1;
    EEPROM.put(EEPROM_COUNTER, messageCounter);
    EEPROM.put(EEPROM_MAGIC,   (uint32_t)EEPROM_MAGIC_VAL);
    EEPROM.commit();
  }
}

void saveCounter(uint32_t val) {
  EEPROM.put(EEPROM_COUNTER, val);
  EEPROM.commit();
}

// ---- ESP-NOW callbacks ------------------------------------
void onSent(uint8_t *mac, uint8_t status) {
  (void)mac;
  (void)status;
}

void onReceive(uint8_t *mac, uint8_t *data, uint8_t len) {
  (void)mac;
  if (len != sizeof(WirePacket)) return;

  uint8_t plain[AES_BLOCKLEN];
  memcpy(plain, data, AES_BLOCKLEN);
#if PLAINTEXT_DEBUG == 0
  AES_ECB_decrypt(&aesCtx, plain);
#endif
  // PLAINTEXT_DEBUG=1: treat received bytes as raw plaintext

  Packet *pkt = (Packet*)plain;
  if (pkt->type == 2 && pkt->sender == ESP.getChipId()) {
    ackReceived = true;
  }
}

// ---- Main -------------------------------------------------
void setup() {
  // ── GPIO2 setup ──────────────────────────────────────────
  // Configure AFTER boot is complete — never before.
  // ESP-01 boot requires GPIO2 HIGH; we set it OUTPUT here
  // which keeps it HIGH by default (ledOff state).
  pinMode(LED_PIN, OUTPUT);
  ledOff();  // ensure LED off on entry

  // ── AES + EEPROM ─────────────────────────────────────────
  AES_init_ctx(&aesCtx, AES_KEY);
  loadCounter();

  // Brief LED pulse to show the chip booted and is working
  ledBlink(1, 80, 0);   // single short flash on button press

  // ── Build packet ─────────────────────────────────────────
  Packet pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.type    = 1;
  pkt.sender  = ESP.getChipId();
  pkt.counter = messageCounter;

  WirePacket wire;
  memcpy(wire.data, &pkt, AES_BLOCKLEN);
#if PLAINTEXT_DEBUG == 0
  AES_ECB_encrypt(&aesCtx, wire.data);
#endif
  // PLAINTEXT_DEBUG=1: packet sent as raw bytes, no encryption

  // ── Radio init ───────────────────────────────────────────
  WiFi.mode(WIFI_STA);
  wifi_set_channel(CHANNEL);
  WiFi.setOutputPower(20.5);

  if (esp_now_init() != 0) {
    // Radio failed — blink error pattern (5 fast), save counter, sleep
    ledBlink(5, 50, 50);
    saveCounter(messageCounter + 1);
    ESP.deepSleep(0);
    return;
  }

  // COMBO: remote must both SEND ring packets AND RECEIVE ACK packets.
  // ESP_NOW_ROLE_CONTROLLER is send-only — the receive callback
  // never fires on a CONTROLLER, so ACKs are silently dropped.
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);
  // COMBO peer: receiver both receives RING packets and sends ACK packets.
  // Registering it as SLAVE tells the SDK we only send to it — incoming
  // packets from that MAC are then filtered out before our callback sees them.
  esp_now_add_peer(receiverMac, ESP_NOW_ROLE_COMBO, CHANNEL, NULL, 0);

  // ── Transmit with retries ────────────────────────────────
  bool confirmed = false;

  for (int attempt = 0; attempt <= MAX_RETRIES; attempt++) {
    ackReceived = false;

    ledOn();   // LED on while waiting for ACK
    int sendResult = esp_now_send(receiverMac, wire.data, sizeof(wire));
    if (sendResult != 0) {
      // SDK rejected send — bad peer state, not a radio issue
      // Blink once and let the retry loop continue
      ledOff(); delay(30); ledOn();
    }

    unsigned long start = millis();
    while (millis() - start < ACK_TIMEOUT_MS) {
      if (ackReceived) break;
      delay(1);
    }
    ledOff();

    if (ackReceived) {
      confirmed = true;
      break;
    }
    if (attempt < MAX_RETRIES) delay(RETRY_BACKOFF_MS);
  }

  // ── LED feedback ─────────────────────────────────────────
  if (confirmed) {
    ledBlink(2, 100, 80);   // 2 blinks = ACK received, all good
  } else {
    ledBlink(4, 40, 40);    // 4 fast blinks = no ACK (receiver unreachable?)
  }

  // ── Save counter and sleep ───────────────────────────────
  // Counter advances regardless of ACK — prevents replay
  // if the receiver got the packet but the ACK was lost.
  saveCounter(messageCounter + 1);

  // Set GPIO2 to INPUT (high impedance) before sleeping.
  // Eliminates leakage current through the LED circuit during
  // deep sleep — matters on a coin cell over months.
  ledOff();
  pinMode(LED_PIN, INPUT);

  ESP.deepSleep(0);   // RST button press wakes from here
}

void loop() {
  // Never reached — all logic in setup() before deep sleep
}
