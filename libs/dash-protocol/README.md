# DASH/5

Jedyny obsługiwany protokół. DASH/4, jego parser i transport datagramowy zostały
usunięte. [Specyfikacja i przykłady](../../docs/dash5/specification.md).

- `protocol.hpp` / `dash5.cpp`: typy, framing, zasoby SHA-256, ścisła walidacja,
  rewizje, idempotencja, rejestr, atomic commit i TTL. Bez rendererów i WMO.
- `unix.hpp` / `dash5_unix.cpp`: prywatne Unix stream, peer UID, deadline,
  reconnect, potwierdzenia, brakujące zasoby i odroczone odpowiedzi.
- `bundle.hpp` / `dash5_bundle.cpp`: ładowanie zaufanych zasobów osadzonych
  w binarce producenta. Nie otwiera plików przekazanych przez usługę.

`Publisher` żyje przez cały czas działania demona. `publish(Json)` przyjmuje
zmienione dane, a `heartbeat()` wysyła ping bez budowania i parsowania snapshotu.
Po reconnect ponawia zapamiętaną publikację, uzupełniając brakujące zasoby.
Wariant `publish(string_view)` służy do wejścia tekstowego. Decoder przekazuje
sparsowane drzewo do odbiornika; transport nie parsuje go ponownie.

`Receiver` przyjmuje callback walidacji zasobów i callback przygotowania
prezentacji. Kompilacja może wewnętrznie zwrócić busy, nie zmieniając stanu.
Serwer zatrzymuje jedno żądanie na połączenie, a zakończenie zadania budzi jego
pętlę. Producent czeka na odpowiedź bez ponawiania publikacji co 25 ms.
Obowiązuje termin operacji 3 s i jeden aktywny kandydat; rozłączenie właściciela
porzuca wynik i zwalnia miejsce po zakończeniu trwającej pracy.
Po udanym przygotowaniu nowa wersja zastępuje starą atomowo. Zasoby aktywnych snapshotów
są przypięte także po rozłączeniu producenta. Nieprzypięty staging jest zwalniany.

Dashboard implementuje callbacki w `ServiceFeed`; protokół nie linkuje UI.
`libs/json` udostępnia niezależny, ścisły parser współdzielony przez UI i protokół.

Testy: `zig build dashboard-test`, `weather-test`, `waste-test` i `test`.
Obejmują rozcięcia strumienia, zasoby, hash, rewizje, utracone ACK, reconnect,
atomiczność, błędy, rzeczywisty transport i niezmienne klatki.
