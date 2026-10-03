# CrisisMesh — Emergency Mesh Network

> **Kategoria:** Defence
>
> Autonomiczny, odporny na jamming i podsłuch system awaryjnej komunikacji radiowej dla mikrokontrolerów ESP32 oraz Raspberry Pi.

---

## O Projekcie

W przypadku nagłej utraty zasilania lub dostępu do internetu (np. w wyniku konfliktów zbrojnych, zmasowanych cyberataków czy katastrof naturalnych) kluczowym wyzwaniem staje się utrzymanie bezpiecznej łączności. **CrisisMesh** to odporny system komunikacji offline, który pozwala na tworzenie lokalnych, podziemnych sieci informacyjnych niepozostawiających żadnego śladu u operatorów telekomunikacyjnych.

System opiera się na modułach **ESP32** zasilanych z małych źródeł energii (powerbanki, dynamo, małe panele solarne), które mogą być ukryte w przestrzeni publicznej. Bezdotykowo i bez pośrednictwa sieci GSM przekazują oraz odbierają zaszyfrowane meldunki, tworząc stabilną siatkę połączeń.

---

## Architektura i Przepływ Danych

Wiadomości wprowadzane są przez interfejs na urządzeniu **Raspberry Pi**, szyfrowane i rozsyłane w eterze do węzłów ESP32, po czym wyświetlane na lokalnych ekranach LCD 16x2.

```text
[ Użytkownik ]
      │
      ▼
[ Interfejs Raspberry Pi ]
      │ (Wprowadzenie komunikatu)
      ▼
[ Szyfrowanie AES-GCM + HW IV ]
      │
      ▼
[ Broadcast ESP-NOW (2.4 GHz) ] ───► [ Szybki Pre-filtr 0xEA51 ]
                                              │
                                              ▼
                                    [ Weryfikacja Anti-Replay Counter ]
                                              │
                                              ▼
                                    [ Deszyfrowanie & Weryfikacja MAC ]
                                              │
                                              ▼
                                    [ Wyświetlacz LCD 16x2 ]
