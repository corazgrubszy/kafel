# Konfiguracja

Ustawienia wdrożenia zapisujesz w ignorowanym `deploy.local.json` na komputerze.
Instalator tworzy pliki środowiska w `/etc/kafel/` na CM4. Przy aktualizacji
zachowuje istniejące ustawienia, jeśli nie nadpiszesz ich w lokalnym pliku.

## Dashboard

| CLI | Zmienna środowiska | Domyślnie |
|---|---|---|
| `--fb=auto\|/dev/fbN` | `KAFEL_FRAMEBUFFER` | `auto` |
| `--touch=auto\|/dev/input/eventN\|off` | `KAFEL_TOUCH` | `auto` |
| `--socket=/absolute/path` | `KAFEL_DASHBOARD_SOCKET` | `/tmp/kafel-<uid>/dashboard.sock` |
| `--idle-timeout=SECONDS` | `KAFEL_IDLE_TIMEOUT` | `60`; zakres 0–86400 s, `0` wyłącza powrót do zegara |

Parametry CLI mają pierwszeństwo przed zmiennymi środowiska.

`--fb=auto` szuka framebuffera zgodnego z rozmiarem i formatem LCD. Nie zakłada
`/dev/fb0`. Jeśli znajdzie dwa pasujące urządzenia, trzeba wskazać jedno przez
`--fb`. `--display-info` wypisuje wybrany framebuffer bez blokowania go i bez
rysowania. Dotyk jest wyszukiwany w `/dev/input/event*`; można wskazać urządzenie
ręcznie albo wyłączyć go przez `--touch=off`.

Deploy zapisuje ustawienia w `/etc/kafel/dashboard.env`. W lokalnej konfiguracji
odpowiadają im pola `framebuffer`, `touch`, `dashboard_socket` i `idle_timeout`,
wszystkie jako napisy. Zmiana ścieżki gniazda wymaga wspólnego wdrożenia
odbiornika i producentów.

## Pogoda

`/etc/kafel/weather.env`:

| Zmienna | Znaczenie |
|---|---|
| `KAFEL_WEATHER_LATITUDE` | Szerokość geograficzna, od −90 do 90 |
| `KAFEL_WEATHER_LONGITUDE` | Długość geograficzna, od −180 do 180 |
| `KAFEL_WEATHER_CA_BUNDLE` | Opcjonalny plik zaufanych CA; domyślnie `/etc/ssl/certs/ca-certificates.crt` |
| `KAFEL_DASHBOARD_SOCKET` | Wspólne gniazdo DASH/5 |

Pola `latitude` i `longitude` w `deploy.local.json` ustawiają współrzędne.
Pogoda korzysta z systemowego zegara i certyfikatów CA do weryfikacji HTTPS.
[Źródło danych, retry i stan nieaktualnych danych](../apps/weather/README.md).

## Śmieci

`/etc/kafel/waste.env`:

| Zmienna | Znaczenie |
|---|---|
| `KAFEL_WASTE_SCHEDULE` | Ścieżka harmonogramu na CM4 |
| `KAFEL_DASHBOARD_SOCKET` | Wspólne gniazdo DASH/5 |

Domyślna ścieżka instalatora to
`/var/lib/kafel-waste/garbage_schedule.json`; pole `waste_schedule` pozwala
wskazać inny istniejący plik. Harmonogram zawiera strefę czasową i korzysta
z systemowej bazy `/usr/share/zoneinfo`.
[Format harmonogramu i wybór odbioru](../apps/waste/README.md).

## Odczyty na ekranie

Zegar pokazuje lokalny czas CM4. Data ma pełną nazwę dnia tygodnia i pomija rok.
Adres IPv4 pochodzi z interfejsu trasy domyślnej, a przy jego braku wiersz zostaje
pusty. CPU i RAM są próbkowane co sekundę; hostname, adres i dysk — co 10 s.
Brak odczytu jest oznaczany jako `--`.

Zajętość dysku dotyczy systemu plików `/`. Wskaźniki liczbowe CPU/RAM/eMMC
kończą się na 99%, aby zachować stałą czcionkę; słupki obejmują pełne 0–100%.

Na kaflu zegara widać zbiorczy stan usług: `OK`, `UWAGA`, `BŁĄD` lub `BRAK`.
Ostrzeżenie zachowuje dane kafla. Błąd źródła pokazuje „Awaria” z jego opisem,
a brak kontaktu przez 15 s — „Offline” i „Brak komunikacji”. Niepoprawna
publikacja nie zastępuje poprzedniej kompozycji ani nie odnawia kontaktu.

[Wdrożenie ustawień](deploy.md) · [Diagnostyka urządzenia](cm4stack.md)
