# Rezultati testiranja dela Studenta 1

Datum testiranja je 17. novembar 2026. Okruženje je Windows x64, MinGW-w64
GCC 14.1.0, C++17, Ninja i Debug konfiguracija. Build je prošao uz uključene
`-Wall -Wextra -Wpedantic`, bez upozorenja.

Komande za ponavljanje su u README-u. Završni CTest prolaz trajao je 2,03 s.
Vremena zavise od računara, antivirusnog softvera i trenutnog opterećenja.

| Provera | Rezultat |
| --- | --- |
| HashMap, 10.000 ključeva, rehash, duplikati, erase i ponovna upotreba | Prošao |
| Big-endian kodiranje i 64-bitne granične vrednosti | Prošao |
| Nepoznata verzija, neispravni brojevi birača | Prošao |
| Podeljen TCP okvir, uzastopni okviri, prekinut okvir i rok prijema | Prošao |
| FIFO sa četiri konkurentna proizvođača i 10.000 poruka | Prošao, svi ID-jevi primljeni jednom, redosled svakog proizvođača očuvan |
| Buđenje blokiranog proizvođača i potrošača pri close | Prošao |
| Država, region, podregion, izborno mesto i nepostojeći cilj | Prošao |
| Ciklusi, nepostojeći roditelj, više korena, dupli ID/endpoint, loš port, rezervisan ID | Prošao |
| Provera identiteta roditelja, greška TS slanja i start iz konfiguracije | Prošao |
| Gašenje pri nepotpunom okviru i blokiranom prijemnom baferu | Prošao |
| 10.000 uzastopnih upita kroz dva propagatora i kontrolisano izborno mesto | Prošao, 1.219 ms |
| Prosleđivanje samostalnog Update kroz TS | Prošao |
| Data Source kao zaseban proces, tri upita i prikaz 37,5000% | Prošao |
| Data Source prikazuje Partial i odbija nepodudarajući requestId | Prošao |
| Pet ponovljenih kreiranja i gašenja stabla | Prošao, procesni handle count 82 pre i 82 posle |

U rekurzivnom testu stvarni TP i TS rade u svojim nitima i prenose poruke
preko lokalnih TCP veza. Testna obrada zamenjuje TC, TOZ, pool i agregaciju
Studenta 2. Svaki upit proverava ID, cilj, presek i očekivane brojeve birača.
Testno izborno mesto vraća 1.000 upisanih i `requestId % 1001` izašlih.
Upiti naizmenično ciljaju region, izborno mesto i državu.

Ovaj rezultat meri deo Studenta 1 sa kontrolisanim odgovorima. Potrebno je
ponoviti funkcionalne testove i test ~10.000 objavljenih podataka kada bude
povezan kompletan deo Studenta 2, uključujući agregaciju više različitih
potomaka, istorijske preseke i timeout aktivnih upita.

Pre konačne odbrane ostaju dogovorena dva stress testa sa asistentom,
VS Profiler, UMDH izveštaj, Performance Counters i PerfView/WinDbg.
Stabilan handle count u ovom kratkom testu predstavlja proveru životnog
ciklusa resursa. UMDH i analiza curenja memorije celog sistema tek slede.
Visual Studio/MSVC build još nije izvršen u ovom okruženju.
