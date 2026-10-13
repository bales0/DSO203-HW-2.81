# Zdroje firmwaru pro DS203 HW 2.81/2.82 s 8MB diskem

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
měřit i stará data; její DC korekce a pořadí B→A musí potvrdit v3.

INT144 v3 zahodí po nastavení ukazatele 152 dvojic a vyhodnotí dalších
512 platných dvojic. Překlad prošel bez varování, 40 numerických kontrol
prošlo v interpretu Pawn na PC. Běh v3 na DS203 zatím neproběhl.
Absolutní přesnost frekvence, napětí a celková analogová kvalita jsou
nadále neměřené; relativní DC korekce zůstávají pouze v RAM.

Experimentální [FPGAW1.2-test](FPGAW1.2-test/README.md) má nyní ve třech
výrazech DP_RAM.v výslovné dvoubitové odečítání kruhového indexu.
Starší simulace nekontrolovala neznámá data; po její opravě prošly
čtyři simulace včetně dvou záznamů bez přenosu staré historie po zahození.
Úplné sestavení a kontrola 72MHz časování prošly. Nový bitstream v build/
má jiný hash a na zařízení se netestoval; W12TEST.BIN v testovacím balíčku
a původní W1.1 zůstaly zachované. FPGALOAD samotné INTERLACE nezapíná.

[LA104](https://github.com/gabonator/LA104) je samostatný alternativní systém.
Jeho aktuální zdroje mají experimentální cíl `DS203HD` pro 4096bajtové
sektory, ale [autor uvádí neodladěnou podporu 8MB DS203](https://github.com/gabonator/LA104/discussions/40)
a hlášení pro HW 2.82 z roku 2024 problém neuzavírá. Proto v této sestavě
není.

Sestavení je popsáno v [BUILDING.md](BUILDING.md). SYS s místním GCC 9.2.1
vytváří obraz po bajtech totožný s autorovým vydáním; finální výstupy jsou v
`bin/SYS164AB/`. Původní obrazy a adresy jsou samostatně v
[reference_builds/](reference_builds/README.md).
