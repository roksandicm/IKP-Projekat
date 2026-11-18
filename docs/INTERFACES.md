# Predlog zajedničkih interfejsa za Studenta 2

Ovaj ugovor proizlazi iz odeljaka 2.2 i 2.3 projektne dokumentacije. Kolega još
nema implementaciju. Njegova potvrda kompatibilnosti ostaje za integraciju.

## Vlasništvo i tok

1. Aplikacija napravi `Winsock`, učita `Topology`, napravi prijemni bafer koji
   implementira `InboundSink`, pa `Transport(topology, nodeId, buffer)`.
2. Pozove `connect_children()` pa `accept_and_start()`. Listovi prethodno moraju
   slušati na svojim portovima. Moguće je i `start(parentSocket)` za već prihvaćenu
   vezu. Start preuzima soket samo pri uspehu. Transport poseduje sve svoje sokete.
3. TP proverava okvir, tip i ID roditelja, pa poziva `InboundSink::push`.
   TP ne obrađuje upit niti računa agregaciju.
4. TC čita isključivo sokete iz `for_each_child` / `child`. Posle validacije
   senderId prema vezi i tipa Result/Update ubacuje
   `ReceivedMessage{message, Origin::Child, childId}` u isti prijemni bafer.
5. TOZ uzima poruke i daje ih jednom od tri radnika. HashMap aktivnih upita i
   poslednja stanja pripadaju Studenta 2 i štite se njegovom kritičnom sekcijom.
6. Za Query radnik napravi `RoutePlan`, pozove `build(topology, selfId, query)`,
   registruje aktivan upit i očekivane child ID-jeve, pa tek onda `dispatch`.
   Deca mogu odgovoriti odmah po slanju. Zapis mora postojati pre dispatch-a.
   Zaključavanje aktivnih upita se oslobađa pre potencijalno blokirajućeg enqueue-a.
7. `dispatch` vraća broj uspešno stavljenih poruka u izlazni red. Ako se razlikuje
   od `count()`, radnik mora završiti upit kao Failed/Partial ili sprovesti timeout.
   Nula odabranih potomaka za nepostojeći cilj znači Result sa statusom NotFound.
8. Rezultat se priprema kao Message i šalje preko `enqueue_parent`. Samostalno
   ažuriranje šalje se istom metodom kao Update sa requestId 0.
   Samo TS radi mrežno slanje, za sve potomke i roditelja.

`enqueue_*` potvrđuje prijem u red. Stvarni neuspeh slanja povećava `failures()`
i gasi odgovarajuću vezu. Student 2 mora koristiti rok aktivnog upita i pratiti
prekide veza. `parent_closed()` označava prekid ili neispravnu poruku roditelja.
TC se gasi kada `receive_message` vrati false. Za idle veze TC prvo proverava
readiness, kao TP, da odsustvo poruka ne smatra istekom roka celog servisa.
Posle neuspelog slanja/prijema delimičnog okvira veza se više ne koristi.

## Gašenje

Zaustaviti prihvatanje novih poslova, pozvati `Transport::stop()`, završiti i
join-ovati TC, TOZ i sve radnike, pa uništiti transport, bafer, topologiju i na
kraju Winsock. `InboundSink::close()` mora probuditi blokirane proizvođače i
potrošače i sprečiti nove push operacije. TS drenira prihvaćene izlazne poruke
uz rok od 3 sekunde po pokušaju slanja. Posle dreniranja gase se veze sa decom.
Zatvaranje soket handle-ova odloženo je do destruktora radi bezbednog
izlaska spoljnih niti. Više istovremenih poziva stop/destrukcije nije podržano.

Transport je jedna sesija. Ponovni start iste instance nije podržan.
`Topology` i lista potomaka se ne menjaju dok rade niti. HashMap sama nema
zaključavanje. Deljene mape Studenta 2 zahtevaju spoljašnju sinhronizaciju.

## Protokol v1

TCP prenosi uzastopne okvire od 64 bajta. Sva polja su unsigned big-endian.

| Offset | Bajtova | Polje |
| --- | --- | --- |
| 0 | 4 | magic 0x494b5035 (`IKP5`) |
| 4 | 4 | version 1 |
| 8 | 4 | type, Query=1 Result=2 Update=3 |
| 12 | 8 | requestId |
| 20 | 4 | senderId neposrednog pošiljaoca |
| 24 | 4 | scope, Country=0 Region=1 Station=2 |
| 28 | 4 | targetId |
| 32 | 8 | cutoff, Unix sekunde, 0 za poslednje stanje |
| 40 | 8 | registeredVoters |
| 48 | 8 | votedVoters |
| 56 | 4 | status, Ok=0 Partial=1 NotFound=2 Failed=3 |
| 60 | 4 | reserved, mora 0 |

Upiti i rezultati imaju nenulti requestId. ID je jedinstven u jednoj roditeljskoj
sesiji i čuva se kroz sve nivoe. `senderId` se menja na svakom skoku. Izvor koristi
UINT32_MAX. Čvorovi koriste ID-jeve 1..UINT32_MAX-1. Query nosi nulte brojeve
birača i status Ok. Result vraća isti scope, target i cutoff kao upit.
Update koristi requestId 0 i status Ok. Za agregirano stanje celog podstabla
može koristiti Country/target 0, a senderId određuje koje podstablo ažurira.
Za Update cutoff predstavlja vreme novog stanja. Na svim nivoima važi
voted <= registered. Za prazan rezultat obe vrednosti su 0.

`encode`/`decode` proveravaju strukturu. Kontekstualne provere radi obrada.
Agregacija prihvata samo očekivane pošiljaoce, sprečava duplikate odgovora,
proverava scope/target/cutoff, proverava prelivanje zbirova i propagira status
Partial/Failed. Sume se računaju sabiranjem brojeva birača. Procenat računa izvor.
Istorijski presek i izbor poslednjeg stanja pre tog vremena pripadaju Studenta 2.

## Topologija i rutiranje

Svaki red ima `nodeId parentId regionId agr|station IPv4 port`. Komentari počinju
znakom `#`. Roditelj korena je 0. Učitavanje proverava jedinstvene ID-jeve i
adrese/portove, jedan koren, postojanje roditelja, cikluse i da list ne bude roditelj.
Koristi se numerički IPv4. Trenutni ugovor ima jednu vezu između roditelja i deteta
i jedan izvor povezan sa korenom. Identitet peer-a određen je konfiguracijom i
očekivanom vezom. Nema autentifikacije ili handshake okvira.

Upit za državu bira svu neposrednu decu. Upit za izborno mesto bira dete koje
sadrži zadati nodeId. Za region se biraju grane koje sadrže čvor sa zadatim
regionId ili pripadaju podstablu takvog čvora. Time region obuhvata i svoje
podregione. Isti algoritam se izvršava na svakom nivou stabla.
RegionId je identifikator oblasti, a nodeId identifikator čvora i izbornog mesta.
ID-jevi iz primera su predlog konfiguracije i mogu se menjati.

Za veliki broj dece TC treba koristiti WSAPoll ili drugi mehanizam koji ne
ograničava broj soketa na FD_SETSIZE. TP nadgleda samo jedan roditeljski soket.
