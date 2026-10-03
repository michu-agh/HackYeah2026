#include <esp_now.h>
#include <WiFi.h>
#include "mbedtls/gcm.h"

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Tajny Klucz AES-256 (Dokładnie 32 bajty - taki sam na wszystkich urządzeniach!)
const unsigned char AES_KEY[32] = "TajnyKluczKryzysowy256Bitow!!!";

// Bufor na pakiet wysyłany przez ESP-NOW
// [12B IV] + [16B TAG] + [max 200B Treści]
uint8_t packetBuffer[228];

esp_now_peer_info_t peerInfo;

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) return;

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("🟢 Nadajnik z szyfrowaniem AES-256-GCM gotowy!");
}

void sendEncryptedBroadcast(const char* plaintext) {
  size_t plaintext_len = strlen(plaintext);

  // 1. Generujemy losowy IV (12 bajtów)
  uint8_t iv[12];
  esp_fill_random(iv, 12);

  // 2. Przygotowujemy bufory na szyfrogram i TAG autentyczności
  uint8_t ciphertext[plaintext_len];
  uint8_t tag[16];

  // 3. Szyfrowanie mbedTLS GCM
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, AES_KEY, 256);

  mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, plaintext_len,
                            iv, 12, NULL, 0,
                            (const unsigned char*)plaintext, ciphertext,
                            16, tag);
  mbedtls_gcm_free(&ctx);

  // 4. Składamy pakiet: IV (12B) + TAG (16B) + Ciphertext
  memcpy(packetBuffer, iv, 12);
  memcpy(packetBuffer + 12, tag, 16);
  memcpy(packetBuffer + 28, ciphertext, plaintext_len);

  size_t total_packet_len = 28 + plaintext_len;

  // 5. Wysyłamy zaszyfrowaną ramkę w eter
  esp_now_send(broadcastAddress, packetBuffer, total_packet_len);
  Serial.printf("🔒 Zaszyfrowano i wysłano w eter %d bajtów.\n", total_packet_len);
}

void loop() {
  sendEncryptedBroadcast("Ewakuacja Sektora A! Zgłoszenie z węzła #1.");
  delay(4000);
}