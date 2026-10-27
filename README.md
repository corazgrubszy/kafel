# Kafel

Dotykowy dashboard dla **M5Stack CM4Stack**. Zegar, stan urządzenia, pogoda
i przypomnienia o odbiorze śmieci na fabrycznym ekranie 240×320 px.

<p>
  <img src="docs/images/dashboard.png" width="240" height="320" alt="Zegar i zasoby systemowe">
  <img src="docs/images/weather.png" width="240" height="320" alt="Temperatura, wiatr i opady">
  <img src="docs/images/waste.png" width="240" height="320" alt="Odbiór śmieci i zapowiedź następnego terminu">
</p>

Przesunięcie palca zmienia duży kafel. CPU, RAM i zajętość eMMC pozostają
widoczne na dole. Po minucie bezczynności wraca zegar.

![Przewijanie kafli](docs/images/ribbon.gif)

Obrazy powstały naszym rendererem z przykładowych danych.
[Stany Śmieci](docs/images/waste-states.png) ·
[Awaria i utrata kontaktu](docs/images/failure-states.png)

## Co jest w środku

| Program | Zadanie |
|---|---|
| `kafel-dashboard` | Ekran, dotyk, zegar, zasoby systemowe i kafle usług |
| [kafel-weather](apps/weather/README.md) | Pogoda z Open-Meteo: temperatura, odczuwalna, wiatr, opady i ilustracja |
| [kafel-waste](apps/waste/README.md) | Lokalny harmonogram odbioru śmieci, kolorowe kubły i zapowiedź kolejnego terminu |

Trzy programy C++20, budowane przez Zig. Dashboard rysuje programowo do
framebuffera. Usługi dostarczają dane i wygląd swoich kafli przez DASH/5.

Projekt jest przygotowany i sprawdzany na **CM4Stack z 64-bitowym Raspberry Pi OS**.
Układ ekranu, obsługa sprzętu i instalacja są dostosowane do tego urządzenia.
Przeniesienie Kafla na inną płytkę lub ekran wymaga osobnego dopasowania i testów.

## Instalacja

Na komputerze przygotuj **Zig 0.17.0, Python 3, Git i OpenSSH** oraz
**clang-format 22** do kontroli stylu w pełnym zestawie testów. Kompilacja
odbywa się na komputerze; na CM4 trafiają statyczne binarki ARM64.
Wdrożenie korzysta z SSH i konta z `sudo` na CM4.

Instalator i audyt wymagają systemowego **Pythona 3 na CM4**. Uruchamianie
usług, rysowanie i zabezpieczenie LCD wykonują statyczne programy C++.
[Szczegóły instalacji](docs/deploy.md).

```sh
git clone https://github.com/corazgrubszy/kafel.git
cd kafel
cp deploy.example.json deploy.local.json
```

W `deploy.local.json` ustaw adres urządzenia, konto SSH i współrzędne Pogody.
Wszystkie wartości są napisami. Przykład wskazuje **operę w Sydney**:

```json
{
  "host": "cm4stack.local",
  "user": "your-user",
  "latitude": "-33.8568",
  "longitude": "151.2153"
}
```

Ten plik jest ignorowany przez Git. Własne współrzędne wpisz tylko w lokalnej
konfiguracji.

Na CM4 przygotuj [harmonogram Śmieci](apps/waste/README.md) w
`/var/lib/kafel-waste/garbage_schedule.json`. Pogoda potrzebuje połączenia
z internetem. Następnie uruchom:

```sh
zig build deploy
```

Polecenie wykonuje testy, buduje i instaluje trzy aplikacje oraz włącza ich
autostart. Hasła SSH i sudo wpisujesz w terminalu. Przy nieudanej instalacji
przywraca poprzednie wydanie. Pierwsza instalacja obejmuje cały zestaw.

## Aktualizacja i kontrola

Każdą aplikację można później aktualizować osobno:

```sh
zig build deploy-dashboard
zig build deploy-weather
zig build deploy-waste
```

Niezmienione aplikacje są pomijane. Wdrożenie jednej usługi nie restartuje
pozostałych. Raporty i kopia do odzyskania poprzedniego wydania trafiają do
lokalnego, ignorowanego przez Git katalogu `deploy-results/`.

Audyt urządzenia:

```sh
python3 scripts/audit.py
```

Sprawdza działające procesy, commit i sumy binarek, logi oraz harmonogram.
[Przebieg wdrożenia i rollback](docs/deploy.md).

## LCD i konsola HDMI

Instalator oddziela ekran dashboardu od konsoli systemowej. Przygotowuje
`/boot/firmware/cmdline.txt` z parametrami:

```text
console=tty2 video=HDMI-A-1:640x480@60D
```

Przed każdym startem dashboardu sprawdza przypisanie konsoli do HDMI.
Bez dostępnego HDMI odłącza fbcon od LCD. Jeśli potrzebny jest reboot,
wdrożenie o tym informuje; nie restartuje urządzenia samo.

Numer LCD jest wykrywany automatycznie — aktualizacja systemu może zmienić
przypisanie `/dev/fbN`. [Konfiguracja i diagnostyka ekranu](docs/cm4stack.md).

## Własne kafle

Usługa publikuje zestaw danych, szablon JSON oraz zasoby SVG przez lokalne
gniazdo Unix. Dashboard przygotowuje obraz i obsługuje przewijanie długich
podpisów. Reguły Pogody i Śmieci pozostają w ich usługach.

[Specyfikacja DASH/5 z przykładem Pogody](docs/dash5/specification.md)

## Dokumentacja

- [Konfiguracja aplikacji i parametry dashboardu](docs/configuration.md)
- [CM4Stack: LCD, dotyk, HDMI i aktualizacje systemu](docs/cm4stack.md)
- [Wdrażanie, systemd i odzyskiwanie poprzedniej wersji](docs/deploy.md)
- [Budowanie, testy i lokalny podgląd](docs/development.md)
- [Architektura i przepływ danych](docs/architecture.md)
- [Biblioteka UI](libs/ui/README.md) · [HttpClient](libs/http-client/README.md)

## Licencja i podziękowania

Kod Kafla jest na [licencji MIT](LICENSE). Wykorzystane biblioteki i grafiki
zachowują własne licencje:

| Składnik | Zastosowanie | Licencja |
|---|---|---|
| stb_truetype | Rasteryzacja fontu | [stb](vendor/stb/LICENSE) |
| IBM Plex Sans Regular/Bold | Typografia | [OFL](libs/ui/assets/ibm-plex-sans/OFL.txt) |
| NanoSVG | Renderowanie SVG | [zlib](vendor/nanosvg/LICENSE.txt) |
| Heroicons Trash Solid | Kubły frakcji, Tailwind Labs | [MIT](vendor/heroicons/LICENSE) |
| Meteocons Flat | Ikony pogody, Bas Milius | [MIT](vendor/meteocons-static/LICENSE) |
| cpp-httplib | Backend HttpClienta | [MIT](vendor/cpp-httplib/LICENSE) |
| Mbed TLS | TLS i certyfikaty | [Licencje Mbed TLS](vendor/mbedtls/LICENSE) |
| picojson | Parser JSON | [BSD](vendor/picojson/LICENSE) |
| Rosé Pine Moon | Paleta, Rosé Pine / mvllow | [MIT](vendor/rose-pine-palette/LICENSE) |
