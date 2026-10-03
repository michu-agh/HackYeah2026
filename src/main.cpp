#include <esp_now.h>
#include <WiFi.h>
#include "mbedtls/gcm.h"
#include <LiquidCrystal.h>

#define LCD_RS 0
#define LCD_E  1
#define LCD_D4 3
#define LCD_D5 4
#define LCD_D6 5
#define LCD_D7 6

LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

const unsigned char AES_KEY[32] =
    "TajnyKluczKryzysowy256Bitow!!!";

uint8_t plaintextBuffer[223];


char lcdMessage[223] = "Czekam na wiadomosc...";
volatile bool newMessage = true;

int scrollPosition = 0;

unsigned long lastScrollTime = 0;

// szybkość przesuwania tekstu
const unsigned long SCROLL_DELAY = 350;

void displayMessage()
{
    int length = strlen(lcdMessage);

    if (length <= 32)
    {
        lcd.clear();

        // pierwsze 16 znaków
        lcd.setCursor(0, 0);

        for (int i = 0; i < 16 && i < length; i++)
        {
            lcd.print(lcdMessage[i]);
        }

        // kolejne 16 znaków
        if (length > 16)
        {
            lcd.setCursor(0, 1);

            for (int i = 16; i < 32 && i < length; i++)
            {
                lcd.print(lcdMessage[i]);
            }
        }

        return;
    }


    if (millis() - lastScrollTime < SCROLL_DELAY)
        return;

    lastScrollTime = millis();

    lcd.clear();

    lcd.setCursor(0, 0);

    for (int i = 0; i < 16; i++)
    {
        int index = scrollPosition + i;

        if (index < length)
            lcd.print(lcdMessage[index]);
        else
            lcd.print(' ');
    }


    lcd.setCursor(0, 1);

    for (int i = 0; i < 16; i++)
    {
        int index = scrollPosition + 16 + i;

        if (index < length)
            lcd.print(lcdMessage[index]);
        else
            lcd.print(' ');
    }

    scrollPosition++;

    // po dojściu do końca zaczynamy od początku
    if (scrollPosition > length)
    {
        scrollPosition = 0;
    }
}


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

    Serial.printf("Rozmiar pakietu: %d B\n", len);

    if (len < 28)
    {
        Serial.println("BLAD: pakiet jest za krotki!");
        return;
    }

    int ciphertext_len = len - 28;

    if (ciphertext_len > 222)
    {
        Serial.println("BLAD: szyfrogram jest za duzy!");
        return;
    }

    uint8_t iv[12];
    uint8_t tag[16];

    memcpy(iv, incomingData, 12);
    memcpy(tag, incomingData + 12, 16);

    const uint8_t *ciphertext = incomingData + 28;

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
===============================================

    if (ret == 0)
    {
        plaintextBuffer[ciphertext_len] = '\0';

        Serial.println("GCM: AUTENTYKACJA OK");

        Serial.print("Tresc: ");

        Serial.println(
            (char *)plaintextBuffer
        );

        strncpy(
            lcdMessage,
            (char *)plaintextBuffer,
            sizeof(lcdMessage) - 1
        );

        lcdMessage[
            sizeof(lcdMessage) - 1
        ] = '\0';

        scrollPosition = 0;

        newMessage = true;
    }

    else
    {
        Serial.println("BLAD GCM!");

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

void setup()
{
    Serial.begin(115200);

    delay(500);

    lcd.begin(16, 2);

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("CRISIS MESH");

    lcd.setCursor(0, 1);
    lcd.print("START...");

    WiFi.mode(WIFI_STA);

    Serial.println();
    Serial.println(
        "Uruchamianie odbiornika ESP-NOW..."
    );

    Serial.print("MAC odbiornika: ");

    Serial.println(
        WiFi.macAddress()
    );

    if (esp_now_init() != ESP_OK)
    {
        Serial.println(
            "BLAD inicjalizacji ESP-NOW!"
        );

        lcd.clear();

        lcd.setCursor(0, 0);
        lcd.print("ESP-NOW ERROR");

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

    // komunikat startowy
    strcpy(
        lcdMessage,
        "Czekam na wiadomosc..."
    );

    scrollPosition = 0;
}

void loop()
{

    if (newMessage)
    {
        scrollPosition = 0;

        lcd.clear();

        newMessage = false;

        lastScrollTime = 0;
    }

    displayMessage();

    delay(5);
}