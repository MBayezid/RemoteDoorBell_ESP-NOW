// ============================================================
//  DOORBELL RECEIVER FIRMWARE — NodeMCU v3 / Lolin (ESP-12F)
//  Features: Multi-remote, pairing mode, AES-128 ECB encryption,
//            EEPROM whitelist persistence, duplicate suppression,
//            Serial debug output (Fire-and-Forget compatible)
// ============================================================
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
extern "C" {
#include <espnow.h>
#include "aes.h"
}

// ---- Debug output -----------------------------------------
#define DEBUG_SERIAL 1
#if DEBUG_SERIAL
#define DBG_BEGIN(baud) Serial.begin(baud)
#define DBG(msg)        Serial.print(msg)
#define DBGLN(msg)      Serial.println(msg)
#define DBGF(...)       Serial.printf(__VA_ARGS__)
#else
#define DBG_BEGIN(baud)
#define DBG(msg)
#define DBGLN(msg)
#define DBGF(...)
#endif

// ---- Plaintext debug mode --------------------------------
// MUST match the same value in remote_main.cpp.
// MUST be 0 for production deployment.
#define PLAINTEXT_DEBUG 1

// ---- Output mode ------------------------------------------
#define OUTPUT_MODE_RELAY         1
#define OUTPUT_MODE_BUZZER_TONE   2
#define OUTPUT_MODE_BUZZER_SIMPLE 3
#define OUTPUT_MODE  OUTPUT_MODE_RELAY   // ← change this line to switch mode

// ---- Configuration ----------------------------------------
#define CHANNEL          2
#ifndef OUTPUT_PIN
#define OUTPUT_PIN       D5        // Relay signal pin or buzzer pin (Use 2 for ESP-01 receiver)
#endif
#ifndef PAIRING_BTN_PIN
#define PAIRING_BTN_PIN  D2        // Active LOW — INPUT_PULLUP, button to GND
#endif
#define RING_DURATION    700       // ms relay held / buzzer on
#define MAX_REMOTES      8
#define PAIRING_WINDOW   10000

#define RELAY_ACTIVE_HIGH 0
#if RELAY_ACTIVE_HIGH
#define RELAY_ON   HIGH
#define RELAY_OFF  LOW
#else
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH
#endif

#define NOTE_DING  1047   // C6
#define NOTE_DONG   784   // G5
#define TONE_MS     300   // ms per note

static const uint8_t AES_KEY[AES_KEYLEN] = {
  0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
  0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};

// ---- EEPROM layout ----------------------------------------
#define EEPROM_SLOT_SIZE  20
#define EEPROM_MAGIC_ADDR (MAX_REMOTES * EEPROM_SLOT_SIZE)
#define EEPROM_MAGIC_VAL  0xCAFEBABEu
#define EEPROM_TOTAL      (EEPROM_MAGIC_ADDR + 4)

// ---- Packet structures ------------------------------------
typedef struct {
  uint8_t  type;
  uint32_t sender;
  uint32_t counter;
  uint8_t  pad[7];
} __attribute__((packed)) Packet;

typedef struct {
  uint8_t data[AES_BLOCKLEN];
} WirePacket;

typedef char _packet_size_check[(sizeof(Packet) == AES_BLOCKLEN) ? 1 : -1];

// ---- Remote state record ----------------------------------
struct RemoteRecord {
  bool     active;
  uint32_t senderID;
  uint32_t lastCounter;
  uint8_t  mac[6];
};

// ---- Globals ----------------------------------------------
RemoteRecord   remotes[MAX_REMOTES];
struct AES_ctx aesCtx;
bool           ringing      = false;
unsigned long  ringStart    = 0;
bool           pairingMode  = false;
unsigned long  pairingStart = 0;

// ---- Forward declarations ---------------------------------
void saveRemote(int idx);
void saveCounter(int idx);
void loadAllRemotes();
void addRemotePeer(uint8_t *mac);
int  findRemote(uint32_t senderID);
int  registerRemote(uint32_t senderID, uint8_t *mac);
void enterPairingMode();
void exitPairingMode();
void triggerOutput();
void onReceive(uint8_t *mac, uint8_t *data, uint8_t len);

// ---- EEPROM persistence -----------------------------------
void saveRemote(int idx) {
  int base = idx * EEPROM_SLOT_SIZE;
  EEPROM.write(base,     remotes[idx].active ? 1 : 0);
  EEPROM.put(base + 1,   remotes[idx].senderID);
  EEPROM.put(base + 5,   remotes[idx].lastCounter);
  for (int b = 0; b < 6; b++) EEPROM.write(base + 9 + b, remotes[idx].mac[b]);
  EEPROM.commit();
}

void saveCounter(int idx) {
  int base = idx * EEPROM_SLOT_SIZE;
  EEPROM.put(base + 5, remotes[idx].lastCounter);
  EEPROM.commit();
}

void loadAllRemotes() {
  EEPROM.begin(EEPROM_TOTAL);
  uint32_t magic = 0;
  EEPROM.get(EEPROM_MAGIC_ADDR, magic);
  if (magic != EEPROM_MAGIC_VAL) {
    DBGLN("[EEPROM] First boot — initialising empty remote table");
    for (int i = 0; i < MAX_REMOTES; i++) memset(&remotes[i], 0, sizeof(RemoteRecord));
    EEPROM.put(EEPROM_MAGIC_ADDR, (uint32_t)EEPROM_MAGIC_VAL);
    EEPROM.commit();
    return;
  }
  int activeCount = 0;
  for (int i = 0; i < MAX_REMOTES; i++) {
    int base = i * EEPROM_SLOT_SIZE;
    remotes[i].active = (EEPROM.read(base) == 1);
    EEPROM.get(base + 1, remotes[i].senderID);
    EEPROM.get(base + 5, remotes[i].lastCounter);
    for (int b = 0; b < 6; b++) remotes[i].mac[b] = EEPROM.read(base + 9 + b);
    if (remotes[i].active) {
      activeCount++;
      DBGF("[EEPROM] Slot %d | ID: 0x%08X | Counter: %u\n", i, remotes[i].senderID, remotes[i].lastCounter);
    }
  }
  DBGF("[EEPROM] %d/%d remote slots active\n", activeCount, MAX_REMOTES);
}

// ---- Peer management --------------------------------------
void addRemotePeer(uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
  int result = esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, CHANNEL, NULL, 0);
  DBGF("[PEER] add %02X:%02X:%02X:%02X:%02X:%02X → %s\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (result == 0) ? "OK" : "FAILED");
}

// ---- Remote table -----------------------------------------
int findRemote(uint32_t senderID) {
  for (int i = 0; i < MAX_REMOTES; i++) {
    if (remotes[i].active && remotes[i].senderID == senderID) return i;
  }
  return -1;
}

int registerRemote(uint32_t senderID, uint8_t *mac) {
  for (int i = 0; i < MAX_REMOTES; i++) {
    if (!remotes[i].active) {
      remotes[i].active      = true;
      remotes[i].senderID    = senderID;
      remotes[i].lastCounter = 0;
      memcpy(remotes[i].mac, mac, 6);
      saveRemote(i);
      addRemotePeer(mac);
      DBGF("[PAIR] New remote registered | Slot %d | ID: 0x%08X\n", i, senderID);
      return i;
    }
  }
  DBGLN("[PAIR] ERROR: Remote table full");
  return -1;
}

// ---- Pairing mode -----------------------------------------
void enterPairingMode() {
  pairingMode  = true;
  pairingStart = millis();
  DBGLN("[PAIR] Pairing mode OPEN — press remote button within 10s");
#if OUTPUT_MODE == OUTPUT_MODE_RELAY
  for (int i = 0; i < 3; i++) { digitalWrite(OUTPUT_PIN, RELAY_ON); delay(80); digitalWrite(OUTPUT_PIN, RELAY_OFF); delay(80); }
#elif OUTPUT_MODE == OUTPUT_MODE_BUZZER_TONE
  for (int i = 0; i < 3; i++) { tone(OUTPUT_PIN, 2000, 80); delay(160); }
#else
  for (int i = 0; i < 3; i++) { digitalWrite(OUTPUT_PIN, HIGH); delay(80); digitalWrite(OUTPUT_PIN, LOW); delay(80); }
#endif
}

void exitPairingMode() {
  pairingMode = false;
  DBGLN("[PAIR] Pairing mode CLOSED");
#if OUTPUT_MODE == OUTPUT_MODE_RELAY
  digitalWrite(OUTPUT_PIN, RELAY_ON); delay(300); digitalWrite(OUTPUT_PIN, RELAY_OFF);
#elif OUTPUT_MODE == OUTPUT_MODE_BUZZER_TONE
  tone(OUTPUT_PIN, 1500, 300); delay(350);
#else
  digitalWrite(OUTPUT_PIN, HIGH); delay(300); digitalWrite(OUTPUT_PIN, LOW);
#endif
}

// ---- Output trigger ---------------------------------------
void triggerOutput() {
#if OUTPUT_MODE == OUTPUT_MODE_RELAY
  digitalWrite(OUTPUT_PIN, RELAY_ON);
  DBGLN("[OUT] Relay energized");
#elif OUTPUT_MODE == OUTPUT_MODE_BUZZER_TONE
  tone(OUTPUT_PIN, NOTE_DING, TONE_MS); delay(TONE_MS + 80); noTone(OUTPUT_PIN);
  delay(100);
  tone(OUTPUT_PIN, NOTE_DONG, TONE_MS); delay(TONE_MS + 80); noTone(OUTPUT_PIN);
  DBGLN("[OUT] Ding-dong played");
#elif OUTPUT_MODE == OUTPUT_MODE_BUZZER_SIMPLE
  digitalWrite(OUTPUT_PIN, HIGH);
  DBGLN("[OUT] Buzzer on");
#endif
}

// ---- ESP-NOW receive callback -----------------------------
void onReceive(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len != sizeof(WirePacket)) return;

  uint8_t plain[AES_BLOCKLEN];
  memcpy(plain, data, AES_BLOCKLEN);
#if PLAINTEXT_DEBUG == 0
  AES_ECB_decrypt(&aesCtx, plain);
#endif

  Packet *pkt = (Packet *)plain;

  if (pkt->type != 1) return; // Ignore non-RING packets

  int idx = findRemote(pkt->sender);
  if (idx == -1) {
    if (!pairingMode) return; // Drop if unknown and not pairing
    idx = registerRemote(pkt->sender, mac);
    if (idx == -1) return;
    remotes[idx].lastCounter = pkt->counter - 1;
  }

  uint32_t delta = pkt->counter - remotes[idx].lastCounter;
  if (delta == 0 || delta > 0xF0000000u) {
    DBGF("[RX] Suppressed duplicate/rollback (delta=%u)\n", delta);
    return; // Silently drop, no ACK needed
  }

  remotes[idx].lastCounter = pkt->counter;
  saveCounter(idx);
  
  DBGF("[RING] *** DING DONG *** remote=0x%08X | counter=%u | slot=%d\n", pkt->sender, pkt->counter, idx);
  
  triggerOutput();
  ringing   = true;
  ringStart = millis();
  
  // NOTE: ACK sending removed for Fire-and-Forget remote compatibility
}

// ---- Entry points -----------------------------------------
void setup() {
#if OUTPUT_MODE == OUTPUT_MODE_RELAY
  digitalWrite(OUTPUT_PIN, RELAY_OFF);
#else
  digitalWrite(OUTPUT_PIN, LOW);
#endif
  pinMode(OUTPUT_PIN, OUTPUT);
  pinMode(PAIRING_BTN_PIN, INPUT_PULLUP);

  DBG_BEGIN(74880);
  delay(100);
  DBGLN("\n\n=============================");
  DBGLN(" Doorbell Receiver v1.0 (Fire-and-Forget)");
  DBGLN("=============================");

  WiFi.mode(WIFI_STA);
  DBGF("  MAC Addr  : %s\n", WiFi.macAddress().c_str());
  DBGF("  Chip ID   : 0x%08X\n", ESP.getChipId());

  AES_init_ctx(&aesCtx, AES_KEY);
  loadAllRemotes();

  wifi_set_channel(CHANNEL);
  WiFi.setOutputPower(20.5);

  if (esp_now_init() != 0) {
    DBGLN("[ESP-NOW] INIT FAILED — rebooting");
    delay(1000);
    ESP.restart();
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onReceive);

  int peersAdded = 0;
  for (int i = 0; i < MAX_REMOTES; i++) {
    if (remotes[i].active) {
      addRemotePeer(remotes[i].mac);
      peersAdded++;
    }
  }
  DBGF("[PEER] %d peer(s) registered from EEPROM\n", peersAdded);
  DBGLN("[READY] Listening for remotes");
}

void loop() {
  if (ringing && (millis() - ringStart > RING_DURATION)) {
#if OUTPUT_MODE == OUTPUT_MODE_RELAY
    digitalWrite(OUTPUT_PIN, RELAY_OFF);
#elif OUTPUT_MODE == OUTPUT_MODE_BUZZER_SIMPLE
    digitalWrite(OUTPUT_PIN, LOW);
#endif
    ringing = false;
    DBGLN("[RING] Output off");
  }

  static unsigned long lastBtnTime = 0;
  static bool lastBtnPressed = false;
  bool btnPressed = (digitalRead(PAIRING_BTN_PIN) == LOW);
  
  if (btnPressed && !lastBtnPressed) {
    unsigned long now = millis();
    if (now - lastBtnTime > 300) {
      lastBtnTime = now;
      pairingMode ? exitPairingMode() : enterPairingMode();
    }
  }
  lastBtnPressed = btnPressed;

  if (pairingMode && (millis() - pairingStart > PAIRING_WINDOW)) {
    exitPairingMode();
  }
}