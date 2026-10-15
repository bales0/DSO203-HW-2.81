# FPGAW12 — diagnostická aplikace QuadPawn

Zdroj: [FPGAW12.pawn](FPGAW12.pawn). Hotová aplikace:
[FPGAW12.amx](../../bin/QuadPawn/Programs/FPGAW12.amx).

Určeno pro DS203 HW 2.81/2.82 s místním QuadPawn navázaným na SYS 1.64
s integrovaným AlterBIOS. Aplikace pracuje s již nakonfigurovaným FPGA W1.1
nebo označeným W1.2. Rodina 0x5731 a protokol 0x0101 zůstávají společné; selector 2 rozlišuje neoznačený kompatibilní profil (0) a W1.2 (0x0102).

## Spuštění na přístroji

Až bude schválen samostatný test na zařízení, zkopírujte FPGAW12.amx na
běžný interní disk DS203 a spusťte jej v prohlížeči programů QuadPawn.
AMX není firmware pro virtuální DFU disk. Přístroj musí mít před spuštěním
kompatibilní W1 FPGA.

Úvodní obrazovka vyžaduje potvrzení Run. Potom se zkontroluje ID;
neodpovídající ID zablokuje záznam. Menu nabízí:

| Volba | Funkce |
| --- | --- |
| ID | ID W1, podverze, nulová odpověď nepoužitého selectoru a 16 opakovaných čtení ID |
| 1MHz | Nastaví vzorkovací takt 1 MHz a čte 256 položek FIFO |
| 72MHz | Nastaví vzorkovací takt 72 MHz a čte 256 položek FIFO |
| Exit | Ukončí program |

Při záznamu jsou kanály dočasně nastaveny na DC, 1 V/div a offset 128,
s nezávislými ADC, bez oversamplingu a s bezpodmínečným triggerem.
Analogová nastavení jsou po dokončení nebo přerušení obnovena. Takt a
trigger nejsou obnoveny na předchozí hodnoty; navazující aplikace musí
nastavit vlastní režim, případně přístroj restartujte.

Program čeká nejvýše 1000 ms na současné příznaky Start a Full. Čekání lze
přerušit tlačítkem. Omezení platí pro smyčku čekání; původní nativní
fpga_read samo nemá DMA timeout. Po každém FIFO čtení vrací H_L do nuly
před kreslením na LCD, stejně jako vzorkovací API runtime.

Výpis obsahuje minimum, maximum a celočíselný průměr syrových 8bitových
hodnot A/B, první datová slova a kombinace digitálních vstupů C/D. Údaj
Configured clock je hodnota vrácená konfigurací časovače; nejde o měření
skutečné vzorkovací frekvence. Statický nebo nulový vstup sám o sobě
neznamená chybu FPGA.

## Bitstream a rozsah testování

Označené W1.2 je v build/FPGAW1.2-test/FIFO.bin a bin/FPGATest/disk/W12TEST.BIN.
Aplikace jej automaticky nenačítá. Nepoužívá fpga_load, obnovu FPGA,
zápis souborů ani zápis firmware či kalibrace. Mění pouze provozní
registry, GPIO a dočasná nastavení vstupů při záznamu.

Důvod: staré QuadPawn Runtime/fpga.c obnovuje původní obraz z adresy
0x0802C000 a čte jen 68 088 bajtů. Pro HW 2.81/2.82 je referenční W1.1
na 0x0805C800 a má 135 096 bajtů. Tento mechanismus nebyl při tvorbě
aplikace upraven. Starší fpga_test.pawn a TESTFPGA.BIN nejsou její součástí.

PASS v aplikaci označuje jen konkrétní kontrolu ID/sběrnice nebo naplnění
FIFO. Uživatel na HW 2.82 potvrdil úspěch testů ID a FIFO při 1 MHz
a 72 MHz s W1.1. Samostatná simulace tb_interlace ověřila pořadí
32 dvojic vzorků v prokládaném i odděleném režimu HDL W1.2-test.
Aplikace FPGAW12 režim INTERLACE nezapíná. Správnost skutečných
analogových dat, všech triggerových režimů, oversamplingu a dosažení
144 MSa/s na DS203 těmito testy potvrzeny nejsou.

## Sestavení a skutečně provedené kontroly

Z adresáře QuadPawn/Programs v PowerShellu:

    & ..\Compiler\bin\pawncc.exe 'FPGAW12.pawn' '-d2' '-v2' '-V1' '-i..\Compiler\include' '-o..\..\bin\QuadPawn\Programs\FPGAW12.amx'

Existující Makefile zahrne nový zdroj automaticky díky seznamu *.pawn.
Není potřeba měnit Makefile ani sestavovat runtime.

Historické údaje sestavení před profily (13. 10. 2026):

- Kompilace Pawn compiler 4.0.4611: úspěšná, bez chyb a varování.
- AMX: 22 565 bajtů, 32bitová hlavička, 19 nativních funkcí; všechny jsou
  registrované v místním runtime.
- Paměťový odhad kompilátoru: 11 408 B včetně největšího overlay a
  4096B prostoru pro zásobník/heap.
- Kontrola importů: žádné fpga_load, zápisy souborů ani fpga_write.
  fpga_write_pins se používá pro provozní sériové rozhraní.
- SHA-256 AMX:
  ae6b37961663e7c43f47503fd3b870d9fde364db215a1508c5901ddd412d577f.

Původní aplikaci uživatel ověřil na DS203. Nové sestavení s profily dosud na zařízení netestováno.
Kontrola importů a kompilace nepotvrzují funkčnost na zařízení.


## Navazující test načítání FPGA

Pro nové dočasné načítání W1.2-test slouží samostatný
[FPGALOAD](FPGALOAD.md) a opravený runtime v bin/QuadPawnTest.
Varování o starém loaderu výše se vztahuje na původní hotový runtime
v bin/QuadPawn. Místní zdroje runtime již obsahují opravu.
Uživatel potvrdil úspěch všech tří testů této aplikace
(ID, 1 MHz, 72 MHz) na HW 2.82 s W1.1 bez chyby a timeoutu.

Aktuální FPGAW12.amx má 25 110 B, odhad RAM 11 736 B, SHA-256
`0f9427ea08906aa498d1165ca730646a06da771c2f4c0084b1f88b57ed6d148d`.

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
