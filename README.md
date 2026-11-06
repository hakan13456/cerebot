# PIC32 ve IoT Tabanlı Akıllı Köy Okulu Kazan Dairesi Sıcaklık ve Nem Takip Sistemi

Necmettin Erbakan Üniversitesi – Bilgisayar Mühendisliği – Uygulama Tasarımı dersi projesi.

Bu depo **vize aşamasını** içerir: internetten bağımsız, yerel güvenlik prototipi.

## Donanım
- Digilent **Cerebot MX4cK** (PIC32MX460F512L, 80 MHz)
- **SHT31** I2C sıcaklık/nem sensörü → J6 (I2C #2: SCL2=RA2, SDA2=RA3), adres 0x44
- UART1 (TX=RF8) → FT232R → PC (PuTTY, 9600 8N1)
- Alarm çıkışı: LD4 (RB13) ve RE0 (JA-01)
- Sessize alma: RB5 (CN7) harici buton (GND'ye), yedek: BTN2 (RA7)

## Yazılım (XC32, register seviyesinde C)
- Timer2 kesmesi ile 10 ms tick, bloklamayan 2 sn örnekleme FSM'i (BEKLE → DÖNÜŞÜM → OKU)
- Eşik alarmı: `TEMP_HIGH` (test: 25 °C, final: 55 °C), `TEMP_LOW` = 5 °C
- Change Notice (CN) kesmesi ile alarm susturma

| Klasör | İçerik |
|---|---|
| `kodlar/01_uart_test_U.c` | UART + LED testi |
| `kodlar/02_sht31_okuma.c` | SHT31 I2C okuma |
| `kodlar/03_timer2_fsm.c` | Timer2 + FSM |
| `kodlar/04_alarm_cn_final.c` | Alarm + CN susturma (vize son hali) |
| `firmware/ilk2.X/` | MPLAB X projesi |
| `ekrangoruntusu/` | Çıktı ekran görüntüleri |

## Kurulum notları
- Programlama: **DEBUG** micro-USB portu (MPLAB IPE v6.20). IPE → Settings → **Release from Reset** seçilmeli.
- Seri çıktı: **UART** micro-USB portu (COM6). J12 jumper'ı **URT** konumunda olmalı.
- Örnek çıktı: `Sicaklik: 20.76 C | Nem: 69.83 %RH | normal`

## Final kapsamı (yapılacak)
WiFi (ESP8266/ESP32) + bulut (ThingSpeak/ThingsBoard), MQ-135, röle/buzzer, nihai eşikler, WDT.
