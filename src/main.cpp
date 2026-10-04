#include <esp_now.h>
#include <WiFi.h>
#include "mbedtls/gcm.h"
#include "esp_system.h"

// =====================================================
// AES
// =====================================================

const unsigned char AES_KEY[32] = "yueF64hQDjUb87k2jqfm59LMG3EW";

uint8_t plaintextBuffer[223];

// =====================================================
// UART RASPBERRY PI
// =====================================================

// ESP32 RX <- TX Raspberry Pi
#define RPI_RX_PIN 20

// ESP32 TX -> RX Raspberry Pi
#define RPI_TX_PIN 21

#define RPI_BAUD 115200

String uartBuffer = "";

// Diagnostyka UART z Raspberry Pi
unsigned long lastRpiDataMs = 0;
unsigned long lastRpiErrorMs = 0;
bool rpiDataSeen = false;
bool rpiLineStarted = false;

const unsigned long RPI_NO_DATA_TIMEOUT_MS = 3000;
const unsigned long RPI_ERROR_REPEAT_MS = 3000;

// =====================================================
// ESP-NOW
// =====================================================

// Na razie BROADCAST.
// Każde ESP32 w zasięgu może odebrać pakiet.
//
// Jeśli macie konkretny MAC odbiornika,
// tutaj można go później wpisać.
uint8_t destinationAddress[] =
{
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};


// =====================================================
// WYSYŁANIE ZASZYFROWANEJ WIADOMOŚCI ESP-NOW
// =====================================================

void sendEncryptedMessage(const char *message)
{
    int messageLength = strlen(message);

    if (messageLength <= 0)
    {
        Serial.println("BLAD: pusta wiadomosc.");
        return;
    }

    // ESP-NOW = max około 250 B
    // 12 B IV + 16 B TAG = 28 B
    // zostaje maksymalnie 222 B tekstu

    if (messageLength > 222)
    {
        Serial.println("BLAD: wiadomosc jest za dluga!");
        return;
    }

    // =================================================
    // IV
    // =================================================

    uint8_t iv[12];

    esp_fill_random(iv, sizeof(iv));

    // =================================================
    // TAG
    // =================================================

    uint8_t tag[16];

    // =================================================
    // CIPHERTEXT
    // =================================================

    uint8_t ciphertext[223];

    // =================================================
    // AES-GCM
    // =================================================

    mbedtls_gcm_context ctx;

    mbedtls_gcm_init(&ctx);

    int ret = mbedtls_gcm_setkey(
        &ctx,
        MBEDTLS_CIPHER_ID_AES,
        AES_KEY,
        256
    );

    if (ret != 0)
    {
        Serial.printf(
            "BLAD ustawiania klucza AES: %d\n",
            ret
        );

        mbedtls_gcm_free(&ctx);

        return;
    }

    // =================================================
    // SZYFROWANIE
    // =================================================

    ret = mbedtls_gcm_crypt_and_tag(
        &ctx,
        MBEDTLS_GCM_ENCRYPT,

        messageLength,

        iv,
        sizeof(iv),

        NULL,
        0,

        (const uint8_t *)message,

        ciphertext,

        sizeof(tag),

        tag
    );

    mbedtls_gcm_free(&ctx);

    if (ret != 0)
    {
        Serial.printf(
            "BLAD szyfrowania GCM: %d\n",
            ret
        );

        return;
    }

    // =================================================
    // BUDOWANIE PAKIETU
    //
    // [ IV 12B ][ TAG 16B ][ CIPHERTEXT ]
    //
    // identyczny format jak oczekuje Twój odbiornik
    // =================================================

    uint8_t packet[250];

    memcpy(
        packet,
        iv,
        12
    );

    memcpy(
        packet + 12,
        tag,
        16
    );

    memcpy(
        packet + 28,
        ciphertext,
        messageLength
    );

    int packetLength =
        28 + messageLength;

    // =================================================
    // ESP-NOW SEND
    // =================================================

    esp_err_t result = esp_now_send(
        destinationAddress,
        packet,
        packetLength
    );

    if (result == ESP_OK)
    {
        Serial.println();
        Serial.println("========== ESP-NOW SEND ==========");
        Serial.print("Wyslano: ");
        Serial.println(message);

        Serial.print("Rozmiar pakietu: ");
        Serial.print(packetLength);
        Serial.println(" B");

        Serial.println("==================================");
    }
    else
    {
        Serial.print(
            "BLAD esp_now_send: "
        );

        Serial.println(result);
    }
}


// =====================================================
// ODBIÓR ESP-NOW
// =====================================================

void OnDataRecv(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int len
)
{
    Serial.println();
    Serial.println(
        "========== ODEBRANO PAKIET =========="
    );

    Serial.printf(
        "MAC nadajnika: %02X:%02X:%02X:%02X:%02X:%02X\n",

        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]
    );

    Serial.printf(
        "Rozmiar pakietu: %d B\n",
        len
    );

    if (len < 28)
    {
        Serial.println(
            "BLAD: pakiet jest za krotki!"
        );

        return;
    }

    int ciphertext_len =
        len - 28;

    if (ciphertext_len > 222)
    {
        Serial.println(
            "BLAD: szyfrogram jest za duzy!"
        );

        return;
    }

    uint8_t iv[12];

    uint8_t tag[16];

    memcpy(
        iv,
        incomingData,
        12
    );

    memcpy(
        tag,
        incomingData + 12,
        16
    );

    const uint8_t *ciphertext =
        incomingData + 28;

    // =================================================
    // AES-GCM
    // =================================================

    mbedtls_gcm_context ctx;

    mbedtls_gcm_init(&ctx);

    int ret =
        mbedtls_gcm_setkey(
            &ctx,
            MBEDTLS_CIPHER_ID_AES,
            AES_KEY,
            256
        );

    if (ret != 0)
    {
        Serial.printf(
            "BLAD ustawiania klucza AES: %d\n",
            ret
        );

        mbedtls_gcm_free(&ctx);

        return;
    }

    // =================================================
    // DESZYFROWANIE
    // =================================================

    ret =
        mbedtls_gcm_auth_decrypt(
            &ctx,

            ciphertext_len,

            iv,
            sizeof(iv),

            NULL,
            0,

            tag,
            sizeof(tag),

            ciphertext,

            plaintextBuffer
        );

    mbedtls_gcm_free(&ctx);

    if (ret == 0)
    {
        plaintextBuffer[ciphertext_len] =
            '\0';

        Serial.println(
            "GCM: AUTENTYKACJA OK"
        );

        Serial.print(
            "Tresc: "
        );

        Serial.println(
            (char *)plaintextBuffer
        );
    }
    else
    {
        Serial.println(
            "BLAD GCM!"
        );

        Serial.println(
            "Pakiet zostal zmodyfikowany lub klucz jest nieprawidlowy."
        );

        Serial.printf(
            "Kod bledu mbedTLS: %d\n",
            ret
        );
    }

    Serial.println(
        "======================================"
    );
}


// =====================================================
// ODBIÓR UART Z RASPBERRY PI
// =====================================================

void readRaspberryUART()
{
    while (Serial1.available())
    {
        char c = Serial1.read();

        // Każdy odebrany bajt oznacza, że UART z RPi żyje.
        lastRpiDataMs = millis();
        rpiDataSeen = true;

        // Pokazuj SUROWE dane z Raspberry Pi na Serial Monitorze USB.
        // Dzięki temu zobaczysz nawet dane bez końcowego '\n'.
        if (!rpiLineStarted)
        {
            Serial.print("[UART RPI] ");
            rpiLineStarted = true;
        }

        if (c == '\r')
        {
            // pomijamy CR w buforze, ale sam fakt odbioru został odnotowany
            continue;
        }

        Serial.write(c);

        // Koniec wiadomości z Raspberry Pi
        if (c == '\n')
        {
            rpiLineStarted = false;

            uartBuffer.trim();

            if (uartBuffer.length() > 0)
            {
                Serial.print("[UART OK] Pelna wiadomosc: ");
                Serial.println(uartBuffer);

                Serial.println("[ESP-NOW] Przekazuje wiadomosc dalej...");

                sendEncryptedMessage(uartBuffer.c_str());

                uartBuffer = "";
            }
        }
        else
        {
            if (uartBuffer.length() < 222)
            {
                uartBuffer += c;
            }
            else
            {
                Serial.println();
                Serial.println("[BLAD UART] Wiadomosc z RPi jest za dluga!");
                uartBuffer = "";
                rpiLineStarted = false;
            }
        }
    }
}

void checkRaspberryUART()
{
    unsigned long now = millis();

    // Pierwsze 3 sekundy po starcie dajemy Raspberry czas na uruchomienie.
    if (now < RPI_NO_DATA_TIMEOUT_MS)
    {
        return;
    }

    bool noData = !rpiDataSeen ||
                  (now - lastRpiDataMs >= RPI_NO_DATA_TIMEOUT_MS);

    if (noData &&
        (now - lastRpiErrorMs >= RPI_ERROR_REPEAT_MS))
    {
        Serial.println("[BLAD UART RPI] Brak danych z Raspberry Pi od co najmniej 3 s.");
        Serial.println("UWAGA: to oznacza brak odebranych bajtow, a nie pewne fizyczne rozlaczenie kabla.");
        lastRpiErrorMs = now;
    }
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
    // =================================================
    // USB / DEBUG
    // =================================================

    Serial.begin(115200);

    delay(500);

    // =================================================
    // UART RASPBERRY PI
    // =================================================

    Serial1.begin(
        RPI_BAUD,
        SERIAL_8N1,
        RPI_RX_PIN,
        RPI_TX_PIN
    );

    Serial.println();
    Serial.println(
        "UART Raspberry Pi uruchomiony."
    );

    Serial.print(
        "RX ESP32: GPIO "
    );

    Serial.println(
        RPI_RX_PIN
    );

    Serial.print(
        "TX ESP32: GPIO "
    );

    Serial.println(
        RPI_TX_PIN
    );

    // =================================================
    // WIFI
    // =================================================

    WiFi.mode(WIFI_STA);

    Serial.println();
    Serial.println(
        "Uruchamianie ESP-NOW..."
    );

    Serial.print(
        "MAC ESP32: "
    );

    Serial.println(
        WiFi.macAddress()
    );

    // =================================================
    // ESP-NOW INIT
    // =================================================

    if (esp_now_init() != ESP_OK)
    {
        Serial.println(
            "BLAD inicjalizacji ESP-NOW!"
        );

        return;
    }

    Serial.println(
        "ESP-NOW uruchomiony."
    );

    // =================================================
    // CALLBACK ODBIORU
    // =================================================

    esp_now_register_recv_cb(
        OnDataRecv
    );

    // =================================================
    // DODANIE PEERA DO WYSYŁANIA
    // =================================================

    esp_now_peer_info_t peerInfo = {};

    memcpy(
        peerInfo.peer_addr,
        destinationAddress,
        6
    );

    peerInfo.channel = 0;

    // AES robimy sami,
    // więc natywne szyfrowanie ESP-NOW wyłączone
    peerInfo.encrypt = false;

    if (!esp_now_is_peer_exist(destinationAddress))
    {
        if (
            esp_now_add_peer(&peerInfo)
            != ESP_OK
        )
        {
            Serial.println(
                "BLAD dodawania ESP-NOW peer!"
            );

            return;
        }
    }

    Serial.println();
    Serial.println(
        "================================="
    );

    Serial.println(
        "ESP32 GOTOWE"
    );

    Serial.println(
        "UART RPI -> AES -> ESP-NOW"
    );

    Serial.println(
        "ESP-NOW -> AES decrypt"
    );

    Serial.println(
        "================================="
    );
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
    // Czytamy wiadomości od Raspberry Pi i pokazujemy je na Serial Monitorze.
    readRaspberryUART();

    // Jeśli przez dłuższy czas nic nie przychodzi, zgłoś błąd diagnostyczny.
    checkRaspberryUART();

    delay(5);
}