# IKP Projekat 5 — Student 1

Implementacija za C++17 / Windows prati dijagram `propagator_agregator_dizajn.png`
i odeljke 2.2–2.3 zajedničke dokumentacije.

## Kompajliranje i testiranje

Iz korena repoa, za instalirani MinGW i Ninja:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Za Visual Studio 2022 sa instaliranim C++ alatima:

```powershell
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Debug
ctest --test-dir build-vs -C Debug --output-on-failure
```

Visual Studio može i direktno otvoriti folder sa `CMakeLists.txt`.
Build folderi su isključeni iz Gita. Validacija je izvršena MinGW kompajlerom.

## Urađeni deo

* `ikp_common` — protokol sa eksplicitnom serijalizacijom, TCP operacije sa rokovima,
  učitavanje i validacija topologije.
* `ikp_student1` — TP, TS, HashMap potomaka, izlazni FIFO i planiranje ruta.
* `data_source` — slanje upita i prikaz rezultata i statusa.
* `student1_tests` — testovi struktura, rutiranja, TCP prenosa, izvršnog programa
  izvora i gašenja. Rekurzivni test šalje 10.000 upita kroz dva propagatora.

Sve kolekcije su sopstvene implementacije. Sinhronizacija koristi Windows
kritične sekcije, condition variables i Interlocked operacije.
Šablonska HashMap i FIFO implementacija su u zaglavljima, a mreža, protokol,
topologija, transport i rutiranje imaju odvojene `.h` i `.cpp` fajlove.

## Pokretanje izvora

Kada kolegin agregator bude povezan i sluša na adresi korena:

```powershell
.\build\data_source.exe 127.0.0.1 27001 country 0 0 1
.\build\data_source.exe 127.0.0.1 27001 region 10 1794900000 1
.\build\data_source.exe 127.0.0.1 27001 station 101 0 10000 10000
```

Argumenti su IPv4, port, opseg, cilj, vremenski presek, broj uzastopnih upita i
opciono maksimalno čekanje odgovora u milisekundama. Presek je Unix vreme u
sekundama, a 0 označava poslednje stanje. Za državu je cilj 0, za region je
`regionId`, a za izborno mesto `nodeId`. Izlaznost se računa iz ukupnih brojeva
upisanih i izašlih. Izvor prikazuje i status delimičnog ili neuspešnog rezultata.
Izvor koristi jednu TCP sesiju, jedinstven requestId unutar te sesije i jedan
aktivan upit istovremeno. Transport podržava više aktivnih ID-jeva.

## Povezivanje sa Studentom 2

Detaljan predlog zajedničkog ugovora je u [docs/INTERFACES.md](docs/INTERFACES.md).
Potrebno je da ga kolega pregleda pre integracije svog dela.

Student 2 implementira Data Destination, TC, prijemni kružni bafer, TOZ,
thread pool sa tri radnika, aktivne upite, agregaciju i istoriju izlaznosti.
Agregator kao kompletan izvršni program zavisi od tih komponenti.
Testni adapteri simuliraju te komponente samo za proveru dela Studenta 1.

Stablo se pokreće od listova ka korenu, pa izvor. Svaki čvor učitava istu
topologiju. Aktivna topologija i veze su nepromenljive do gašenja procesa.
Posle prekida veze aplikacija završava sesiju i ponovo se pokreće uz novu
instancu transporta. Automatsko ponovno povezivanje nije deo ovog protokola.

VS Profiler, UMDH, Performance Counters, PerfView/WinDbg i dva stress testa
dogovorena sa asistentom ostaju za proveru integrisanog projekta.
Rezultati trenutnih testova su u [docs/TEST_RESULTS.md](docs/TEST_RESULTS.md).
