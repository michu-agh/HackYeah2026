#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include "mbedtls/gcm.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "esp_idf_version.h"

// =====================================================
// OLED SSD1306 128x64 I2C
// =====================================================

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

#define OLED_SDA 4
#define OLED_SCL 5

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
    "TajnyKluczKryzysowy256Bitow!!!";

uint8_t plaintextBuffer[223];

// =====================================================
// WIADOMOSC OLED
// =====================================================

char oledMessage[223] = "Czekam na wiadomosc...";
char pendingMessage[223];

volatile bool newMessage = false;

// =====================================================
// PAGINACJA OLED
// =====================================================

// Font Adafruit przy textSize(1):
// około 21 znaków w wierszu
// 8 wierszy na ekranie
//
// 21 * 8 = ~168 znaków
//
// Dajemy trochę mniej, żeby było bezpiecznie.

const int CHARS_PER_PAGE = 150;

int currentPage = 0;
int totalPages = 1;

unsigned long lastPageChange = 0;

const unsigned long PAGE_TIME = 3000;

// =====================================================
// WYSWIETLENIE STRONY WIADOMOSCI
// =====================================================

void displayPage()
{
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);

    int length = strlen(oledMessage);

    int start =
        currentPage * CHARS_PER_PAGE;

    int end =
        start + CHARS_PER_PAGE;

    if (end > length)
    {
        end = length;
    }

    // ============================
    // Tekst wiadomości
    // ============================

    for (int i = start; i < end; i++)
    {
        display.print(oledMessage[i]);
    }

    // ============================
    // Numer strony
    // ============================

    if (totalPages > 1)
    {
        display.setCursor(90, 56);

        display.print(currentPage + 1);
        display.print("/");
        display.print(totalPages);
    }

    display.display();
}

// =====================================================
// NOWA WIADOMOSC
// =====================================================

void showNewMessage()
{
    int length = strlen(oledMessage);

    totalPages =
        (length + CHARS_PER_PAGE - 1)
        / CHARS_PER_PAGE;

    if (totalPages < 1)
    {
        totalPages = 1;
    }

    currentPage = 0;

    lastPageChange = millis();

    displayPage();
}

// =====================================================
// AKTUALIZACJA OLED
// =====================================================

void updateOLED()
{
    // ==========================================
    // Odebrano nową wiadomość
    // ==========================================

    if (newMessage)
    {
        strncpy(
            oledMessage,
            pendingMessage,
            sizeof(oledMessage) - 1
        );

        oledMessage[
            sizeof(oledMessage) - 1
        ] = '\0';

        newMessage = false;

        showNewMessage();

        return;
    }

    // ==========================================
    // Jeśli jest więcej niż jedna strona
    // automatycznie przełączamy
    // ==========================================

    if (totalPages > 1)
    {
        if (
            millis() - lastPageChange
            >= PAGE_TIME
        )
        {
            lastPageChange = millis();

            currentPage++;

            if (currentPage >= totalPages)
            {
                currentPage = 0;
            }

            displayPage();
        }
    }
}

// =====================================================
// OBSLUGA ODEBRANEGO PAKIETU
// =====================================================

void handleReceivedPacket(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int len
)
{
    Serial.println();
    Serial.println(
        "========== ODEBRANO PAKIET =========="
    );

    // =================================================
    // MAC
    // =================================================

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
    // Minimalny pakiet:
    //
    // 12 B IV
    // 16 B TAG
    // =================================================

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

    // =================================================
    // PODZIAL PAKIETU
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
    // AES-256-GCM
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

    // =================================================
    // POPRAWNA WIADOMOSC
    // =================================================

    if (ret == 0)
    {
        plaintextBuffer[
            ciphertext_len
        ] = '\0';

        Serial.println(
            "GCM: AUTENTYKACJA OK"
        );

        Serial.println(
            "Pakiet nie zostal zmodyfikowany."
        );

        Serial.print("Tresc: ");

        Serial.println(
            (char *)plaintextBuffer
        );

        // =============================================
        // Kopiujemy tekst dla OLED
        // =============================================

        strncpy(
            pendingMessage,
            (char *)plaintextBuffer,
            sizeof(pendingMessage) - 1
        );

        pendingMessage[
            sizeof(pendingMessage) - 1
        ] = '\0';

        newMessage = true;
    }

    // =================================================
    // BLAD GCM
    // =================================================

    else
    {
        Serial.println(
            "BLAD GCM!"
        );

        Serial.println(
            "Pakiet zostal zmodyfikowany"
        );

        Serial.println(
            "lub klucz jest nieprawidlowy."
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
// CALLBACK ESP-NOW
//
// Obsługa Arduino ESP32 2.x oraz 3.x
// =====================================================

#if ESP_IDF_VERSION_MAJOR >= 5

void OnDataRecv(
    const esp_now_recv_info_t *info,
    const uint8_t *incomingData,
    int len
)
{
    handleReceivedPacket(
        info->src_addr,
        incomingData,
        len
    );
}

#else

void OnDataRecv(
    const uint8_t *mac,
    const uint8_t *incomingData,
    int len
)
{
    handleReceivedPacket(
        mac,
        incomingData,
        len
    );
}

#endif

// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(115200);

    delay(500);

    // =================================================
    // I2C
    // =================================================

    Wire.begin(
        OLED_SDA,
        OLED_SCL
    );

    // =================================================
    // OLED
    // =================================================

    if (
        !display.begin(
            SSD1306_SWITCHCAPVCC,
            OLED_ADDRESS
        )
    )
    {
        Serial.println(
            "BLAD OLED!"
        );

        while (true)
        {
            delay(100);
        }
    }

    // =================================================
    // EKRAN STARTOWY
    // =================================================

    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(
        SSD1306_WHITE
    );

    display.setCursor(0, 0);

    display.println(
        "CRISIS MESH"
    );

    display.println();

    display.println(
        "OLED OK"
    );

    display.println(
        "Start systemu..."
    );

    display.display();

    delay(1500);

    // =================================================
    // WIADOMOSC OCZEKIWANIA
    // =================================================

    strcpy(
        oledMessage,
        "Czekam na wiadomosc..."
    );

    showNewMessage();

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

    if (
        esp_now_init()
        != ESP_OK
    )
    {
        Serial.println(
            "BLAD inicjalizacji ESP-NOW!"
        );

        display.clearDisplay();

        display.setCursor(0, 0);

        display.println(
            "CRISIS MESH"
        );

        display.println();

        display.println(
            "ESP-NOW ERROR!"
        );

        display.display();

        return;
    }

    Serial.println(
        "ESP-NOW uruchomiony."
    );

    esp_now_register_recv_cb(
        OnDataRecv
    );

    Serial.println(
        "Odbiornik AES-256-GCM gotowy."
    );

    Serial.println(
        "Czekam na pakiety..."
    );
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
    updateOLED();

    delay(5);
}