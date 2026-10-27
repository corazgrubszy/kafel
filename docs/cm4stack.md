# CM4Stack: ekran, dotyk i diagnostyka

Kafel jest rozwijany dla fabrycznego ekranu i dotyku CM4Stack. Poniższe
parametry opisują używany backend i pomagają w diagnostyce po aktualizacji
systemu. Obsługa innych płytek i ekranów nie jest obecnie testowana.

## Ekran i dotyk CM4Stack

Dashboard domyślnie szuka jedynego zgodnego LCD przez ioctl wszystkich
framebufferów. `--fb=/dev/fbN` pozwala wskazać urządzenie jawnie. Akceptuje
240×320, little-endian RGB565, packed pixels/truecolor i brak przesunięcia
widocznego obrazu. Obsługuje padding w stride oraz znane oznaczenie
`nonstd=1` sterownika `fb_st7789v`. Inne formaty są odrzucane przed rysowaniem.
Aplikacja nie instaluje sterownika ekranu ani overlayów Device Tree.

Dotyk jest wykrywany w `/dev/input/event*` po możliwościach urządzenia,
nie po stałym numerze `event1`. Musi udostępniać `INPUT_PROP_DIRECT`, `ABS_X/Y`
i `BTN_TOUCH`. Niedostępny lub niezgodny dotyk zatrzymuje start aplikacji, chyba że
świadomie ustawiono `--touch=off` / `KAFEL_TOUCH=off`.

```sh
ls -l /dev/fb0 /dev/input/event*
cat /proc/fb
cat /sys/class/graphics/fb0/bits_per_pixel
id
/opt/kafel/current/bin/kafel-dashboard --check-input
```

Konto usług musi należeć do grup z dostępem do odpowiednich urządzeń.
Jednostka dashboardu deklaruje `SupplementaryGroups=video input`.

## Kto może pisać do LCD

Framebuffer ma blokadę `flock`, która zapobiega drugiej współpracującej
instancji dashboardu. Jest to blokada doradcza: nie powstrzyma konsoli
jądra ani innego programu ignorującego blokadę. LCD powinien mieć jednego
aktywnego autora. Nie uruchamiaj drugiego dashboardu obok usługi.

### Automatyczna izolacja przy starcie usługi

`ExecStartPre` uruchamia jako root statyczny `kafel-display-console`; właściwy
renderer pracuje jako zwykłe konto. Narzędzie:

1. Wykrywa i waliduje LCD tym samym kodem `Framebuffer::resolve` co dashboard,
   z ustawieniem `KAFEL_FRAMEBUFFER` z tego samego pliku środowiska.
2. Szuka osobnego `vc4drmfb` powiązanego z kartą DRM z HDMI-A-1.
   Wymaga także wymuszonego trybu HDMI w bieżącym `/proc/cmdline`.
3. Jeśli cel jest jednoznaczny, mapuje wszystkie 63 konsole wirtualne na HDMI
   i odczytuje mapowanie ponownie. LCD nie jest celem żadnej z nich.
4. Jeśli nie ma potwierdzonego HDMI, odłącza framebufferową konsolę przez
   sysfs i sprawdza wynik. Nie zgaduje na podstawie `fb1`.
5. Jeśli operacja się nie powiedzie, odtwarza poprzedni stan i kończy się
   błędem — dashboard nie startuje z niepotwierdzoną ochroną.

Nie działa żaden dodatkowy daemon ani pętla odrysowująca napisy. Stan i kopia
przypisania konsoli są w `/run/kafel-display/`, dostępnym wyłącznie rootowi.
Izolacja pozostaje po zatrzymaniu lub awarii dashboardu; nie przywracamy
tekstu na LCD podczas restartu procesu. Przy każdym starcie ochrona jest
sprawdzana ponownie. Po reboot katalog `/run` powstaje od nowa.

```sh
sudo cat /run/kafel-display/status.json
sudo journalctl -u kafel-dashboard.service -b --no-pager
```

`mode: hdmi` podaje osobne urządzenie konsoli; `mode: detached` oznacza
bezpieczny wariant bez ekranowej konsoli — diagnostyka pozostaje przez SSH,
journal i skonfigurowany port szeregowy. Wariant awaryjny odłącza całe fbcon,
więc obejmuje również inne jego ekrany. Służy do dedykowanego urządzenia Kafel.

Aby świadomie przywrócić przypisanie sprzed pierwszej izolacji w tym boot:

```sh
sudo systemctl stop kafel-dashboard.service
sudo /opt/kafel/libexec/kafel-display-console restore
```

Następny start dashboardu ponownie zastosuje ochronę. Pozostaje ograniczenie
fbdev: inny program z prawem zapisu, ignorujący `flock`, nadal może rysować
bezpośrednio do LCD. Nie należy równolegle uruchamiać takich programów.
Ponowne podpięcie konsoli przez administratora lub przeładowanie sterowników
w trakcie działania wymaga ponownego sprawdzenia izolacji/restartu usługi.

### HDMI bez monitora i transakcja wdrożenia

Gdy `/boot/firmware/cmdline.txt` istnieje i DRM zgłasza HDMI-A-1, instalator:

- zachowuje `root=`, konsolę szeregową i pozostałe parametry;
- usuwa wcześniejsze `fbcon=map...`, w tym niepełne `fbcon=map`;
- ustawia konsolę wirtualną `console=tty2`;
- dodaje `video=HDMI-A-1:640x480@60D` — wymusza cyfrowy output także bez monitora.

Znaczenie `D`: [dokumentacja trybów DRM](https://docs.kernel.org/fb/modedb.html).
Nie wpisujemy numeru HDMI framebuffera do cmdline; ustalamy go po uruchomieniu.
Istniejący inny tryb HDMI-A-1 zatrzymuje preflight, zamiast zostać nadpisany.
Plik musi zawierać jedną niepustą linię i dokładnie jeden parametr `root=`.
Instalator nie zmienia `config.txt`, sterowników ani getty.

Oryginalny plik trafia do lokalnego `rollback.tar.gz` wraz z mapą ścieżek
w `rollback.json`; błąd transakcji przywraca go automatycznie. Nie powstaje
kolejny prywatny backup w repo ani harmonogramie. Instalator nie wykonuje
reboot. Do czasu restartu LCD chroni odłączenie fbcon; po restarcie narzędzie
próbuje skierować konsolę na rzeczywiście utworzony framebuffer HDMI.

Po wdrożeniu i uzgodnionym restarcie sprawdź:

```sh
cat /proc/fb
cat /proc/cmdline
sudo cat /run/kafel-display/status.json
```

Należy zweryfikować start bez kabla HDMI oraz po podłączeniu monitora.
Jeżeli sterownik nie tworzy potwierdzonego HDMI, raport pozostanie `detached`;
nie uznajemy tego za dowód działania konsoli HDMI. Stan ochrony i parametry
ekranu są także w raporcie `python3 scripts/audit.py`.

### Konsola systemowa i cmdline.txt

W Raspberry Pi OS parametry kernela są w `/boot/firmware/cmdline.txt`.
Sprawdź rzeczywiście użyte parametry i dostępne framebuffery:

```sh
cat /proc/cmdline
cat /proc/fb
systemctl status getty@tty1.service getty@tty2.service --no-pager
```

Przykładowy fragment przed zmianą:

```text
console=serial0,115200 console=tty1
```

Historyczny fragment z poprzedniej konfiguracji (nie zalecenie nowego wdrożenia):

```text
console=serial0,115200 console=tty2 fbcon=map:2
```

**To nie są dwa sposoby wskazania tty2.** `console=tty2` wybiera konsolę
komunikatów kernela. `fbcon=map:2` mapuje konsole wirtualne na **framebuffer
nr 2**, a nie na tty2. Jeśli fb2 nie istnieje, zapobiega przejęciu konsoli
przez dostępny framebuffer. Gdy fb2 istnieje lub pojawi się później, efekt
będzie inny. Nie jest to uniwersalny parametr „zarezerwuj LCD”.
Znaczenie mapowania opisuje [dokumentacja fbcon](https://docs.kernel.org/fb/fbcon.html).

Przed edycją zrób kopię:

```sh
sudo cp -a /boot/firmware/cmdline.txt /boot/firmware/cmdline.txt.before-kafel
sudo nano /boot/firmware/cmdline.txt
```

Zmień tylko właściwe parametry konsoli, zachowując jedną linię i pozostałe
ustawienia, zwłaszcza `root=`, `rootfstype=` i `rootwait`. Nie kopiuj cudzej
całej linii z identyfikatorem partycji. Dobierz mapowanie do `/proc/fb`;
samo przeniesienie `console=tty1` na `tty2` nie gwarantuje zwolnienia LCD.
Usługi getty i konsola fbcon są oddzielnymi źródłami tekstu na ekranie.

Po restarcie ponownie sprawdź `/proc/cmdline`, `/proc/fb` i obraz LCD.
Przywrócenie kopii oraz kolejny restart wycofują zmianę. Nowy instalator
zarządza opisanymi wyżej parametrami i ochroną konsoli; historyczny przykład
z tej sekcji nie zastępuje wdrożenia.

`kafel-dashboard --check` nie zapisuje pikseli, lecz wymaga blokady fb.
Na działającym urządzeniu wykonaj go w oknie serwisowym:

```sh
sudo systemctl stop kafel-dashboard.service
/opt/kafel/current/bin/kafel-dashboard --check
sudo systemctl start kafel-dashboard.service
```

Uruchomienie drugiego procesu bez zatrzymania usługi powinno zakończyć się
błędem blokady — nie oznacza to uszkodzenia ekranu.

## Brak /dev/fb0 po aktualizacji

Zacznij od logów:

```sh
sudo journalctl -u kafel-dashboard.service -b -n 60 --no-pager
sudo journalctl -k -b --no-pager | rg -i 'gpio9|st7789|Failed to request te'
```

Jeśli na urządzeniu nie ma `rg`, użyj `grep -Ei` z tym samym wzorcem.
Rozpoznany wcześniej konflikt ma komunikaty:

```text
pin gpio9 already requested by fe204000.spi
Failed to request te GPIO
probe with driver fb_st7789v failed with error -22
```

W tej konfiguracji overlay M5Stack używa GPIO9 jako TE, a SPI0 rezerwuje ten
sam pin jako MISO. Nie należy utożsamiać każdego braku framebuffera z tym
konfliktem — najpierw sprawdź powyższe komunikaty.
Historyczny kontekst: zmiana
[Enable strict pinmux mode](https://github.com/raspberrypi/linux/commit/825dba6c63eeb40a62699d1f8a4aa3f02b0eaf49).

Gdy ekran korzysta z SPI0 bez odczytu MISO, opcja
[`no_miso` w spi0-1cs](https://github.com/raspberrypi/linux/blob/rpi-6.18.y/arch/arm/boot/dts/overlays/spi0-1cs-overlay.dts)
zwalnia tę linię. Najpierw zachowaj kopię:

```sh
sudo cp -a /boot/firmware/config.txt /boot/firmware/config.txt.before-kafel
sudo nano /boot/firmware/config.txt
```

W aktywnej sekcji, przed overlayem M5Stack:

```ini
dtoverlay=spi0-1cs,no_miso
dtoverlay=m5stack-cm4
```

Jeśli wpis `spi0-1cs` już istnieje, dopisz opcję zamiast dublować overlay.
Ta poprawka dotyczy konfiguracji bez urządzeń wymagających MISO; nie wyłączaj
całego SPI. Po zapisaniu wykonaj reboot, sprawdź `/proc/fb`, 16 bpp i stan
`kafel-dashboard.service`. Przy kolejnej aktualizacji zweryfikuj zachowanie
opcji w aktywnej sekcji. Wycofanie polega na przywróceniu kopii i restarcie.

## Usługa działa, ale dane są błędne

```sh
systemctl status kafel-dashboard.service kafel-weather.service kafel-waste.service --no-pager
sudo journalctl -u kafel-dashboard.service -u kafel-weather.service -u kafel-waste.service -b -n 80 --no-pager
/opt/kafel/current/bin/kafel-dashboard --system-info
```

- `services: registered ...` potwierdza przyjęcie pierwszego snapshotu producenta.
- `OFFLINE` oznacza brak przyjętego heartbeat, nie automatycznie awarię Internetu.
- `Brak danych` Pogody oznacza brak poprawnego cache; sprawdź HTTPS, CA, zegar
  i log pobierania. Retry odbywa się automatycznie.
- `Brak harmonogramu` Śmieci oznacza błąd odczytu/walidacji bez starego cache;
  `Brak terminów` — poprawny harmonogram bez przyszłych odbiorów.
- `kafel-waste --check /absolute/path/schedule.json` sprawdza plik bez publikowania;
  uruchom jako konto usługi, aby sprawdzić również prawa odczytu.

Konfiguracja i logi mogą zawierać lokalne dane. Do zgłoszeń problemów dołączaj
potrzebny fragment po usunięciu prywatnych adresów i lokalizacji.
