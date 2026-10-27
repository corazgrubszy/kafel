# Budowanie i testy

Narzędzia na komputerze: Zig 0.17.0 i Python 3. Do pełnego zestawu testów
potrzebny jest też clang-format 22. Zig dostarcza kompilator C++
i linkuje biblioteki z `vendor/`. Do wdrażania dochodzą Git oraz OpenSSH.
Na CM4 nie trzeba instalować kompilatora. Polecenia w tym dokumencie uruchamiaj
z katalogu głównego repozytorium.

## Build dla CM4Stack i lokalne programy

| Polecenie | Wynik |
|---|---|
| `zig build` | Wszystkie trzy aplikacje dla CM4Stack oraz helper konsoli |
| `zig build dashboard` | `zig-out/linux-arm64/kafel-dashboard` i `kafel-display-console` |
| `zig build weather` | `zig-out/linux-arm64/kafel-weather` |
| `zig build waste` | `zig-out/linux-arm64/kafel-waste` |
| `zig build weather-native` | Lokalny `zig-out/bin/kafel-weather` |
| `zig build waste-native` | Lokalny `zig-out/bin/kafel-waste` |

Build dla CM4Stack używa ARM64, musl i ReleaseSmall. Programy natywne służą
do lokalnego rozwoju; dla nich i testów można wybrać np. `-Doptimize=ReleaseFast`.
Biblioteki i fonty są wkompilowane w binarki.

`zig build deploy` buduje i instaluje cały zestaw. Targety `deploy-dashboard`,
`deploy-weather` i `deploy-waste` wybierają właściwy build oraz testy aplikacji.

## Podgląd bez urządzenia

Z katalogu repozytorium:

```sh
zig build test -Doptimize=ReleaseFast
zig build preview -Doutput=/tmp/kafel.ppm -Doptimize=ReleaseFast
zig build preview -Dscene=weather -Doutput=/tmp/kafel-weather.ppm -Doptimize=ReleaseFast
zig build preview -Dscene=waste -Doutput=/tmp/kafel-waste.ppm -Doptimize=ReleaseFast
```

Podgląd używa widoku i renderera aplikacji oraz stałego zestawu przykładowych
danych. Wynikiem jest plik PPM 240×320.

Galerię z tego README odtworzysz poleceniem:

```sh
python3 scripts/render_previews.py
```

Skrypt wymaga Pillow. Renderer C++ rysuje obrazy, a Pillow zapisuje je jako
PNG i GIF. Animacja trwa 6 sekund; gesty są odtwarzane przez kontroler dotyku
używany w dashboardzie.

Dodatkowe narzędzia do odtwarzania ikon są opisane w [katalogu Pogody](../apps/weather/assets/icons/README.md).
Zwykły build korzysta z zasobów zapisanych w repo.

## Testy

```sh
zig build test -Doptimize=ReleaseFast
zig build dashboard-test -Doptimize=ReleaseFast
zig build weather-test -Doptimize=ReleaseFast
zig build waste-test -Doptimize=ReleaseFast
zig build deploy-test
```

Testy obejmują parser, limity zasobów, TTL, kalendarz, retry, gesty, typografię,
clipping i transakcje wdrażania. Integracje używają lokalnych serwerów HTTP
oraz uruchomionych demonów.

Osobno można sprawdzić klienta HTTPS i certyfikaty:

```sh
zig build --build-file libs/http-client/build.zig test -Doptimize=ReleaseSmall
```

Te testy wymagają `cryptography` i TLS 1.3. Interpreter można wskazać przez
`-Dpython=/path/to/python3`. Pomiary rozmiaru binarki i pamięci klienta na CM4
są w [README HttpClienta](../libs/http-client/README.md).

## Wersje aplikacji

`--version` każdej aplikacji zawiera `git=<pełny SHA>`. Build z lokalnymi zmianami
ma dopisek `-dirty`, a z archiwum bez `.git` — `git=unknown`. Pliki ignorowane
przez Git nie zmieniają oznaczenia. Identyfikator jest ustalany podczas budowania;
po commicie trzeba ponownie zbudować binarki.

## Narzędzia administracyjne i helper konsoli

`install.py` i `audit.py` korzystają z systemowego Pythona 3 na CM4 podczas
wdrażania i kontroli urządzenia. Systemd uruchamia usługi oraz helper izolacji
konsoli bez interpretera.

`zig build dashboard` buduje również statyczny `kafel-display-console`.
Instalator umieszcza go w `/opt/kafel/libexec/`. Kod helpera jest w
`apps/dashboard/src/platform/linux/console/`:

- `isolation.cpp`: polityka HDMI/detach, odczyt DRM i walidacja stanu;
- `system.cpp`: ioctl/sysfs i wykrywanie LCD współdzielone z `Framebuffer`;
- `state.cpp`: blokada, prywatny stan JSON i zapis atomowy;
- `main.cpp`: polecenia `isolate`, `restore` oraz `--version`.

Testy używają fałszywego adaptera i tymczasowego drzewa sysfs. Sprawdzają
mapowanie 63 VT, readback, rollback po błędzie ioctl i zapisu stanu, odrzucanie
symlinków oraz zachowanie oryginalnego mapowania między startami usługi.
Nie otwierają konsoli komputera budującego.

[Izolacja konsoli](cm4stack.md) · [Instalator](deploy.md)

Zapis C/C++ określa `.clang-format`; używamy clang-format 22, żeby wynik nie
zależał od wersji narzędzia. `zig build test` obejmuje kontrolę formatowania.
Można ją też uruchomić osobno: `zig build format-check`. Poprawki stosuje
`python3 scripts/check_format.py --fix`. Skrypt sprawdza śledzone pliki C/C++,
łącznie z testami, przykładami i nagłówkami w `packaging/`, pomijając `vendor/`.
Wyszukuje clang-format w PATH i standardowych lokalizacjach Homebrew;
zmienna `CLANG_FORMAT` pozwala wskazać plik wykonywalny.
Brak właściwej wersji zatrzymuje kontrolę zamiast po cichu ją pomijać.

Kod zegara i jego testy układu są w
`apps/dashboard`, a wspólne dekodowanie UTF-8 w `libs/json/include/text`.
`zig build visual-test` sprawdza zgodność pikseli z zatwierdzoną wersją;
format protokołu i przykładowe dane pozostają niezależnymi regresjami.

Rzeczywisty worker i jego test są w `dashboard/async_compiler.*` oraz
`apps/dashboard/tests/async_compiler_test.cpp`. Motyw i geometria dużych kafli
są w `apps/dashboard/src/dashboard`, a `libs/ui` przyjmuje ogólne style.
Testy odrzucenia kafla wymagają konkretnego kodu błędu; nie zaliczają dowolnego
wyjątku jako poprawnego odrzucenia.

`ServiceCard` jest sprawdzaną wartością. Przeniesienie zachowuje pełny kontrakt
w obiekcie docelowym; źródło wolno wyłącznie zniszczyć lub przypisać mu nową
kartę. Nie odczytujemy go ani nie przekazujemy do `replace_services` przed
ponownym przypisaniem. Testy obejmują przenoszenie i ponowne użycie przez
przypisanie.

Budżet przygotowania 2000 ms jest miękki: kompilator sprawdza go po całym
kaflu, bez przerywania renderowania. Spóźniony wynik nie jest publikowany;
worker pozostaje zajęty do zakończenia pracy. Test z kontrolowanym zegarem
wykonuje rzeczywiste renderowanie i sprawdza granicę 2000/2001 ms.
