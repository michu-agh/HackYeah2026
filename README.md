# CrisisMesh RPi Control Center

Minimalny, działający szkielet aplikacji na Raspberry Pi:

- backend C++17,
- Crow HTTP + WebSocket,
- komunikacja USB/UART z ESP32 Gateway,
- JSON jako format RPi -> Gateway,
- panel WWW bez Reacta i bez Node.js,
- live eventy ESP32 -> przeglądarka.

## 1. Pakiety na Raspberry Pi OS

```bash
sudo apt update
sudo apt install -y build-essential cmake git libasio-dev
```

Jeżeli port `/dev/ttyACM0` nie jest dostępny dla zwykłego użytkownika:

```bash
sudo usermod -aG dialout $USER
```

Potem wyloguj się i zaloguj ponownie.

## 2. Budowanie

W katalogu projektu:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

Przy pierwszej konfiguracji CMake pobierze Crow i nlohmann/json.

## 3. Uruchomienie

Domyślnie:

```bash
./build/crisismesh
```

używa:

```text
/dev/ttyACM0
115200 baud
```

Możesz podać własny port i baudrate:

```bash
./build/crisismesh /dev/ttyUSB0 115200
```

## 4. Otwórz panel

Na Raspberry Pi:

```text
http://localhost:8080
```

Z innego urządzenia w tej samej sieci:

```text
http://ADRES_IP_RPI:8080
```

## 5. Protokół RPi -> ESP32 Gateway

Każda ramka to jeden JSON zakończony `\n`, np.:

```json
{
  "cmd": "SEND",
  "id": "19ABC-1234-0",
  "type": "EVACUATION",
  "priority": 3,
  "ttl": 6,
  "timestamp": 1791060000000,
  "title": "Ewakuacja",
  "message": "Udaj się do punktu zbiórki."
}
```

Na ESP32 wystarczy czytać Serial do znaku nowej linii i sparsować JSON.

Jeżeli Wasz istniejący firmware używa innego formatu, zmieńcie tylko fragment
oznaczony komentarzem `THIS IS THE SERIAL PROTOCOL BOUNDARY` w `src/main.cpp`.

## 6. ESP32 -> RPi

Backend przyjmuje dowolną linię z ESP.

Jeżeli ESP wyśle JSON:

```json
{"type":"ACK","messageId":"ABC","node":"NODE_04"}
```

zostanie on od razu przekazany WebSocketem do panelu.

Jeżeli ESP wyśle zwykły tekst:

```text
ACK|ABC|NODE_04
```

backend też go pokaże w panelu jako `SERIAL_LINE`.

## 7. Ważne

Limit komunikatu w szkielecie to 180 znaków. To celowo konserwatywny limit
dla komunikacji embedded. Jeśli macie własną fragmentację pakietów, zwiększcie
`MAX_MESSAGE_LENGTH` w `src/main.cpp` i `maxlength` w `web/index.html`.
