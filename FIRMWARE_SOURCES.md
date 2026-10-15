# Zdroje firmwaru pro DS203 HW 2.81/2.82 s 8MB diskem

Aktuální místní vývoj (15. 10. 2026): [Wildcat V6.6](WildcatV6.6/README.md)
přidává běžné položky 144D/144A u vstupu A, osm rozsahů a kalibraci
ve společném sériovém WPT se zálohou WPB. K1+K4/K3+K4 byly odstraněny;
společná standardní kalibrace A+B+144 se otevírá dlouhým K2 na A nebo B.
Stejná DC reference se připojuje na oba vstupy; pro 144 se její kroky
ručně neopakují. Zápis se provádí až po potvrzení baterie/napáječe.
Blikání již neovládá změny kalibračních koeficientů ani referenční hodnoty. Výchozí připravená instalace používá [FPGA W1.2](bin/FPGAW1_2/README.md),
odvozené z W1.1 v FPGAW1.2-test/. Instalační raw obraz má CRC 66B80BD2,
stejnou délku a adresu jako W1.1. Zůstává zachován původní protokol selector1=0101,
nová značka je selector2=0102. [QuadPawn 2026.10.14.2](bin/QuadPawnW12/README.md)
je nad stejným upstreamem a přijímá obě známé ROM zálohy. SYS se neměnil.
Nová integrace 144D/144A a společné uložení WPT ještě nejsou ověřené na DS203.
Uživatel potvrdil instalaci W1.2 přes ADR/BIN a běh předchozího Wildcatu;
FPGA HEX hlásil ERR. Identifikace ve FPGALOAD se nadále diagnostikuje.
Následující revize a původní reference zůstávají zachované.


Tento pracovní strom obsahuje Wildcat V6.5b, SYS 1.64 s integrovaným
AlterBIOS, samostatný QuadPawn a FPGA W1.1. Uvedené revize jsou určeny pro
následující opravy. Převzetí zdrojů samo nepotvrzuje provoz na konkrétním
přístroji ani bezchybné zápisy do externí flash.

| Součást | Místo | Zdroj a revize |
| --- | --- | --- |
| Wildcat V6.5b | `WildcatV6.5b/` | [Turro75/DS203-DSOQuad, fix-CFG-save](https://github.com/Turro75/DS203-DSOQuad/tree/60614c5b071cfc63fcf339e237f63e9a991e3bbf/WildcatV6.5); `Files.c` a `Main.c` jsou převzaty po bajtech, ostatní místní zdroje aplikace zůstaly; místní Makefile/linker/build skript zůstaly kvůli sestavení |
| SYS 1.64 + AlterBIOS | `SYS_V1.64_AlterBIOS/gcc/` | [Turro75/DSO203_SYS164_GCC, insertAlterbios](https://github.com/Turro75/DSO203_SYS164_GCC/tree/b56b6b3f1c96f62070bdf1f4feebaee01493af4c); převzaté zdroje, místní Makefile a README upravené pro `bin/SYS164AB/` |
| QuadPawn, samostatný runtime | `QuadPawn/` | [Turro75/QuadPawn, sysgcc_mergedalterbios](https://github.com/Turro75/QuadPawn/tree/f702d570c8cafcd94cbbf90a4eba080a5d1d3784), navazuje na [původní projekt Petteriho Aimonena](https://github.com/PetteriAimonen/QuadPawn); převzaté zdroje, finální výstupy v `bin/QuadPawn/` |
| FPGA W1.1 | `W1.1 FPGA/` | [MotoMaxis/DS203-DSOQuad](https://github.com/MotoMaxis/DS203-DSOQuad/tree/ef08bdb195742fb88077a59bb898916aeef7ba5f/WildcatV6.5/W1.1%20FPGA); Verilog, `Pins.pcf`, `Time.sdc` a původní bitstream v `bin/FPGAW1_1/FPGAW1_1.BIN` |
| LOGO pro HW 2.81/2.82 | `reference_builds/LOGO/` | Obnoveno z místního `recovery/RECOVERY281282.zip`; obsahuje instalační pár `.ADR/.BIN` a upravitelný obraz `.BMP` vyňatý z BIN; [původ a kontrolní součty](reference_builds/LOGO/README.md) |

SHA-256 stažených zdrojových ZIPů: Wildcat V6.5b
`b98eecc2ce30ec86df26a240c40a64c0ca6cbec3174670eadaffc02801fec65d`,
SYS `6a6bee07e917b3216c1768c7b19663e600fea8306c12be4c2662efc77ee5b4c3`,
QuadPawn `c83cdfdc2ff8c01d7aaa062af0662b5307566317325aebbb9eb24855d95fb634`.
Zdrojový obraz FPGA W1.1 `bin/FPGAW1_1/FPGAW1_1.BIN` má SHA-256
`bf86d13b51bee7fce5d5f82906ea9086091fbee66a52d08622245d4838279739`.

## Vazba SYS a QuadPawn

Sloučený SYS umisťuje tabulku funkcí AlterBIOS na `0x08004400`. Její značka
je `0x017A0025` a pořadí všech 25 symbolů odpovídá zdrojům aktuálního
QuadPawn. Vydaný [SYSGCCA](https://github.com/Turro75/DSO203_SYS164_GCC/releases/tag/v1.0)
obsahuje tutéž značku; autor uvádí testování na HW 2.82.
Samostatný AlterBIOS s API na `0x08009400` byl odstraněn, protože k této
větvi QuadPawn nepatří. SYS a QuadPawn se sestavují nezávisle.

## DFU pro HW 2.81/2.82

Nejnovější **doložená** verze bootloaderu DFU pro tento HW je V3.46C.
Místní `recovery/RECOVERY281282.zip` obsahuje `DS203.DFU.V346C.bin`
(16 384 B, SHA-256 `3614a64867245bca7413b09e9723243ca1016eea9057837c5f7c9ab6316b368f`)
i `.hex` (SHA-256 `1fc1b77e5c0cbac158cd2ba7a6403255371414cd7e267a99c6d7753a5c6d246a`).
Obsahuje také starší V3.45C. [Zkušenost s HW 2.82 a V3.46C](https://forum.seeedstudio.com/t/latest-firmware-for-hw-v2-82/6358)
uvádí podporu této verze; [výrobní fórum](https://forum.minidso.com/forum.php?mod=viewthread&tid=905)
popisuje starší V3.45C pro HW 2.81. Novější ověřený DFU pro DS203
HW 2.81/2.82 ani jeho zdrojový kód se nepodařilo dohledat. DFU zde
zůstává pouze v původním recovery archivu jako referenční binární obraz.
Zápis bootloaderu vyžaduje jiný postup než běžná aktualizace aplikace
přes virtuální DFU disk a nebyl na zařízení ověřen.

## Hardware a omezení

Wildcat V6.5b je určen pro HW 2.81 a novější. Jeho 8MB úprava mění ukládání
parametrů v `Files.c`; další chyby souborového systému a zápisu do flash jsou
před provozem na zařízení stále předmětem kontroly. Oproti W6.5 může změna
ovlivnit práci s uživatelsky pojmenovanými konfiguracemi; potřebuje praktický
test. Sestavení Wildcat není test funkce flash ani měření.

Původní hotový runtime v bin/QuadPawn obnovuje FPGA ze staré adresy
0x0802C000 s délkou 68 088 bajtů; jeho loader pro HW 2.81/2.82 nepoužívejte.
Místní zdroje QuadPawn/Runtime nyní obsahují opravu na 0x0805C800,
135 096 bajtů, kontrolu CRC a obnovu po ukončení aplikace. Oddělené
sestavení je v bin/QuadPawnTest, s volitelným parametrem VERSION
(výchozí 2026.10.13). [FPGALOAD](QuadPawn/Programs/FPGALOAD.md) popisuje
balíček a 16 regresních testů. Uživatel potvrdil na HW 2.82 testy
FPGAW12 s W1.1, spuštění nového runtime PAWN_TST.HEX ze slotu 4,
načtení W1.2-test aplikací FPGALOAD, testy ID/FIFO při 1 MHz a 72 MHz
a návrat k W1.1. Nahrání runtime se podařilo z jiného počítače.

FPGA W1.1 přidává plnorychlostní oversampling a umožňuje opačnou fázi
hodin ADC B. SYS a QuadPawn umějí zapnout režim INTERLACE. Uživatel na
HW 2.82 naměřil INT144 v2 při 1/2/4 MHz vždy S=0, I=−4 a Delta=−4
v osminách periody 72MHz taktu, bez ořezaných vzorků. Tyto hodnoty
jsou zachovány v [INT144.md](QuadPawn/Programs/INT144.md), ale dodatečná
kontrola odhalila chybějící zahození předtriggerové historie. V2 mohla
měřit i stará data; tyto výsledky zůstávají pouze historické.

INT144 v3 zahodí po nastavení ukazatele 152 dvojic a vyhodnotí dalších
512 platných dvojic. Překlad prošel bez varování a 40 numerických kontrol
prošlo v interpretu Pawn na PC. Uživatel 14. 10. 2026 na HW 2.82 při
1/2/4 MHz znovu naměřil vždy S=0, I=−4 a Delta=−4 bez ořezání. Čistý
test ověřil prokládání při nominálních 144 MSa/s a pořadí B→A. DC korekce
Q10 jsou S=1078/−3514 a I=1052/−885, pouze v RAM. Podle dosavadního
postupu byl aktivní původní W1.1: FPGALOAD jej při ukončení obnovuje
a INT144 bitstream nenačítá. Sdílené ID samo přesný obraz nerozlišuje.
Při změně FPGA je nutné opakovat test fáze, FIFO a DC korekce. Absolutní
přesnost frekvence, napětí a celková analogová kvalita zůstávají neměřené.

Experimentální [FPGAW1.2-test](FPGAW1.2-test/README.md) má nyní ve třech
výrazech DP_RAM.v výslovné dvoubitové odečítání kruhového indexu.
Starší simulace nekontrolovala neznámá data; po její opravě prošly
čtyři simulace včetně dvou záznamů bez přenosu staré historie po zahození.
Označené W1.2 přidává selector 2 = 0x0102 a zachovává selector 1 = 0x0101 pro Wildcat. Úplné sestavení se seedem 3 prošlo (13,14 ns / 76,12 MHz). Balíček W12TEST.BIN nyní obsahuje nový obraz, CRC 33AFAD10, SHA-256 787fff5254366c370ed571da5a5c606986160806a6e1e99164718aa5ef381ddb. Na zařízení se netestoval; stará baseline je v reference/W12BASE.BIN. Původní W1.1 zůstalo beze změny. FPGALOAD samotné INTERLACE nezapíná.

[LA104](https://github.com/gabonator/LA104) je samostatný alternativní systém.
Jeho aktuální zdroje mají experimentální cíl `DS203HD` pro 4096bajtové
sektory, ale [autor uvádí neodladěnou podporu 8MB DS203](https://github.com/gabonator/LA104/discussions/40)
a hlášení pro HW 2.82 z roku 2024 problém neuzavírá. Proto v této sestavě
není.

Sestavení je popsáno v [BUILDING.md](BUILDING.md). SYS s místním GCC 9.2.1
vytváří obraz po bajtech totožný s autorovým vydáním; finální výstupy jsou v
`bin/SYS164AB/`. Původní obrazy a adresy jsou samostatně v
[reference_builds/](reference_builds/README.md).

## Profily FPGA v Pawn aplikacích (14. 10. 2026)

FPGALOAD, FPGAW12 a INT144 používají společný `fpgaver.inc`. Před každým
záznamem znovu ověří identitu a potřebné funkce; neznámý profil měření
zablokuje. Schopnosti jsou přiřazeny podle známých zdrojů, nejde o jejich
automatické elektrické ověření. Neoznačený kompatibilní obraz nelze podle
registrů jednoznačně odlišit od jiného obrazu se stejnými odpověďmi.

| Údaj | W1.1 kompatibilní, neoznačený | W1.2 |
| --- | --- | --- |
| Selector 0, rodina | 0x5731 | 0x5731 |
| Selector 1, protokol | 0x0101 | 0x0101 |
| Selector 2, značka implementace | 0 | 0x0102 |
| Deklarované funkce | FIFO, prokládání ADC, oversampling, 32bitový trigger, FreeRun | Stejné, navíc značka verze |

Wildcat se nemění: používá původní údaj selectoru 1 pro povolení FPGA
oversamplingu a novou značku selectoru 2 nečte. Změna rozlišování verzí
se týká uvedených Pawn aplikací, nikoli Wildcatu ani QuadPawn runtime.

Na PC prošlo 92 kontrol profilů ve skutečné Pawn VM a 16 kontrol loaderu.
Loaderové testy přebírají očekávané CRC z FPGALOAD, nikoli z testovaného
souboru. Spuštění z kořene: `./tests/fpga_profile/run.ps1` a
`./tests/fpga_loader/run.ps1`. Celé aplikace s perifériemi na PC neběží.
Nová sestavení AMX s profily dosud nebyla ověřena na DS203.

## Samostatný Wildcat V6.6 a About QuadPawn (14. 10. 2026)

[WildcatV6.6](WildcatV6.6/README.md) je nová samostatná vývojová kopie V6.5b
s detekcí FPGA, zobrazením profilu a přípravným rozhraním 144 MSa/s.
V měřicí smyčce zatím režim nezapíná. Výstupy jsou v bin/WildcatV6.6/.
Wildcat V6.5b se nemění. Předchozí poznámka o nezměněném Wildcatu
se týká V6.5b a kroku zavádění profilů do Pawn aplikací.

[QuadPawnTest](bin/QuadPawnTest/README.md) byl následně samostatně sestaven
s About 2026.10.14 a původem Turro75 f702d57. Zdrojová základna ověřené
větve je stále aktuální, dva commity před upstreamem PetteriAimonen.
Změna About nemění API runtime. Nové výstupy V6.6 a runtime nebyly na zařízení testovány.
