// ============================================================
//  DOORBELL REMOTE FIRMWARE — ESP-01 (512KB)
//  FIRE-AND-FORGET MODE (Power-button triggered)
// ============================================================
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
extern "C" {
#include <espnow.h>
#include "aes.h"
}

// ---- Debug mode -------------------------------------------
#define PLAINTEXT_DEBUG 1

// ---- Pin definitions --------------------------------------
#define LED_PIN       2     // GPIO2 (Active LOW on ESP-01)

// ---- Configuration ----------------------------------------
#define CHANNEL       2     
#define TX_RETRIES    3     

// ⚠️ UPDATE THIS: Paste the MAC address from your receiver's serial monitor

// uint8_t receiverMac[] = {0x68, 0xc6, 0x3a, 0xd6, 0x59, 0x48};
// MAC: bc:ff:4d:34:e1:c3
uint8_t receiverMac[] = {0xBC, 0xFF, 0x4D, 0x34, 0xE1, 0xC3};

// AES-128 shared key
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
  uint8_t  type;      // 1 = RING
  uint32_t sender;    // chip ID
  uint32_t counter;   // rolling counter
  uint8_t  pad[7];    // padding to 16 bytes
} __attribute__((packed)) Packet;

typedef struct {
  uint8_t data[AES_BLOCKLEN];
} WirePacket;

// ---- Globals ----------------------------------------------
uint32_t       messageCounter = 0;
struct AES_ctx aesCtx;

// ---- LED helpers ------------------------------------------
inline void ledOn()  { digitalWrite(LED_PIN, LOW);  }
inline void ledOff() { digitalWrite(LED_PIN, HIGH); }

// ---- EEPROM counter persistence ---------------------------
void loadCounter() {
  EEPROM.begin(EEPROM_SIZE);
  uint32_t magic = 0;
  EEPROM.get(EEPROM_MAGIC, magic);
  if (magic == EEPROM_MAGIC_VAL) {
    EEPROM.get(EEPROM_COUNTER, messageCounter);
  } else {
    messageCounter = 1;
    EEPROM.put(EEPROM_COUNTER, messageCounter);
    EEPROM.put(EEPROM_MAGIC, (uint32_t)EEPROM_MAGIC_VAL);
    EEPROM.commit();
  }
}

void saveCounter(uint32_t val) {
  EEPROM.put(EEPROM_COUNTER, val);
  EEPROM.commit();
  // CRITICAL: Wait for flash write to complete
  delay(100); // Increased to 100ms for maximum safety on older flash chips
}

// ---- Main -------------------------------------------------
void setup() {
  // 1. GPIO2 setup
  pinMode(LED_PIN, OUTPUT);
  ledOff();

  // 2. Init AES + EEPROM
  AES_init_ctx(&aesCtx, AES_KEY);
  loadCounter();

  // ✅ CRITICAL FIX: Increment and save the counter IMMEDIATELY.
  // By doing this BEFORE WiFi initialization and transmission, we ensure
  // the EEPROM write completes as early as possible in the boot sequence.
  // This drastically reduces the chance of a "desync" if the user releases
  // the power button too quickly.
  messageCounter++;
  saveCounter(messageCounter);

  // Quick LED pulse to show the chip booted
  ledOn(); delay(50); ledOff();

  // 3. Build packet using the ALREADY INCREMENTED counter
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

  // 4. Radio init
  WiFi.mode(WIFI_STA);
  wifi_set_channel(CHANNEL);
  WiFi.setOutputPower(20.5);

  if (esp_now_init() != 0) {
    // Radio failed. Counter is already safely incremented.
    ESP.deepSleep(0);
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_add_peer(receiverMac, ESP_NOW_ROLE_COMBO, CHANNEL, NULL, 0);

  // 5. Fire-and-forget transmission
  for (int i = 0; i < TX_RETRIES; i++) {
    esp_now_send(receiverMac, wire.data, sizeof(wire));
    delay(30); 
  }

  // 6. Sleep (Counter was already saved at the beginning)
  ESP.deepSleep(0);
}

void loop() {
  // Never reached
}