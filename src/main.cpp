#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>

#include "mbedtls/gcm.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =====================================================
// OLED SSD1306 128x64 I2C
// =====================================================

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

#define OLED_SDA 8
#define OLED_SCL 9

#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
    OLED_WIDTH,
    OLED_HEIGHT,
    &Wire,
    -1
);

// =====================================================
// AES
// =====================================================

const unsigned char AES_KEY[32] =
    "yueF64hQDjUb87k2jqfm59LMG3EW";

uint8_t plaintextBuffer[223];

// =====================================================
// OLED - prosta funkcja wyswietlania
// =====================================================

void showOLED(const char *title, const char *text = "")
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // automatyczne zawijanie tekstu
    display.setTextWrap(true);

    display.setCursor(0, 0);

    display.println(title);

    if (strlen(text) > 0)
    {
        display.println();
        display.println(text);
    }

    display.display();
}

// =====================================================
// ODBIOR ESP-NOW
// =====================================================

void OnDataRecv(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int len
)
{
    Serial.println();
    Serial.println("========== ODEBRANO PAKIET ==========");

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

    // =================================================
    // Sprawdzenie minimalnego rozmiaru
    // 12B IV + 16B TAG = 28B
    // =================================================

    if (len < 28)
    {
        Serial.println(
            "BLAD: pakiet jest za krotki!"
        );

        showOLED(
            "BLAD RX",
            "Pakiet za krotki"
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

        showOLED(
            "BLAD RX",
            "Pakiet za duzy"
        );

        return;
    }

    // =================================================
    // Rozpakowanie pakietu
    //
    // [ IV 12B ][ TAG 16B ][ CIPHERTEXT ]
    // =================================================

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

        showOLED(
            "BLAD AES",
            "SETKEY"
        );

        return;
    }

    // =================================================
    // Deszyfrowanie + sprawdzenie TAG
    // =================================================

    ret = mbedtls_gcm_auth_decrypt(
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

    // =================================================
    // POPRAWNA WIADOMOSC
    // =================================================

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

        // =============================================
        // WYSWIETLENIE WIADOMOSCI NA OLED
        // =============================================

        showOLED(
            "ODEBRANO:",
            (char *)plaintextBuffer
        );
    }

    // =================================================
    // BLAD AES / TAG
    // =================================================

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

        showOLED(
            "BLAD GCM",
            "Pakiet odrzucony"
        );
    }

    Serial.println(
        "======================================"
    );
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(115200);

    delay(500);

    Serial.println();
    Serial.println(
        "Uruchamianie Crisis Mesh Receiver..."
    );

    // =================================================
    // I2C
    // =================================================

    Wire.begin(
        OLED_SDA,
        OLED_SCL
    );

    Serial.println(
        "I2C uruchomione."
    );

    Serial.print(
        "SDA: GPIO "
    );

    Serial.println(
        OLED_SDA
    );

    Serial.print(
        "SCL: GPIO "
    );

    Serial.println(
        OLED_SCL
    );

    // =================================================
    // OLED
    // =================================================

    if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDRESS
    ))
    {
        Serial.println(
            "BLAD inicjalizacji OLED!"
        );

        // NIE robimy return.
        // ESP-NOW nadal moze dzialac.
    }
    else
    {
        Serial.println(
            "OLED uruchomiony."
        );

        showOLED(
            "CRISIS MESH",
            "Receiver start..."
        );

        delay(1000);
    }

    // =================================================
    // WIFI
    // =================================================

    WiFi.mode(
        WIFI_STA
    );

    Serial.println();

    Serial.println(
        "Uruchamianie odbiornika ESP-NOW..."
    );

    Serial.print(
        "MAC odbiornika: "
    );

    Serial.println(
        WiFi.macAddress()
    );

    // =================================================
    // ESP-NOW
    // =================================================

    if (esp_now_init() != ESP_OK)
    {
        Serial.println(
            "BLAD inicjalizacji ESP-NOW!"
        );

        showOLED(
            "ESP-NOW ERROR",
            "Init failed"
        );

        return;
    }

    Serial.println(
        "ESP-NOW uruchomiony."
    );

    // =================================================
    // CALLBACK
    // =================================================

    esp_now_register_recv_cb(
        OnDataRecv
    );

    Serial.println(
        "Odbiornik AES-256-GCM gotowy."
    );

    Serial.println(
        "Czekam na pakiety..."
    );

    // =================================================
    // OLED - stan gotowosci
    // =================================================

    showOLED(
        "CRISIS MESH",
        "Czekam na wiadomosc..."
    );
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
    // ESP-NOW odbiera asynchronicznie.
    // Po odebraniu pakietu uruchamiany jest OnDataRecv().

    delay(100);
}