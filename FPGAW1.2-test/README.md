# FPGA W1.2 — experimentální vývoj pro DS203 HW 2.81/2.82

Pro trvalou náhradu W1.1 je nově připravený samostatný
[bin/FPGAW1_2](../bin/FPGAW1_2/README.md). `export-system.py` ověří přesný
hash označeného testovacího obrazu a odstraní pouze čtyřbajtový úvod,
aby raw BIN měl původní délku 135 096 B a začínal sync slovem na stejné
adrese 0x0805C800. CRC této instalační varianty je 66B80BD2, CRC plného
W12TEST.BIN zůstává 33AFAD10. HDL ani rozmístění se tím nemění.
Nový [Wildcat 144](../WildcatV6.6/README.md) vyžaduje instalační variantu;
trvalé nahrání a nová kontinuální smyčka na zařízení zatím neproběhly.


W1.2 vychází z původního W1.1 pro Lattice iCE40HX4K-CB132. V DP_RAM.v
jsou tři výrazy kruhového indexu výslovně dvoubitové. IO_Ctrl.v přidává
značku implementace na selectoru 2. Ostatní tři HDL soubory a Time.sdc
zůstávají shodné s W1.1. Pins.pcf zachovává všech 45 fyzických pinů;
změnil se pouze zápis vektorových názvů pro nextpnr. Původní W1.1 se nemění.

Balíček `bin/FPGATest/disk/W12TEST.BIN` nyní obsahuje označené W1.2.
Má 135 100 B, CRC-32 `33AFAD10`, SHA-256
`787fff5254366c370ed571da5a5c606986160806a6e1e99164718aa5ef381ddb`.
Dříve na zařízení ověřený obraz je zachován jako
`bin/FPGATest/reference/W12BASE.BIN` (CRC-32 `46EDF657`).
Finální označené W1.2 dosud na zařízení otestováno nebylo.

## Nástroje na tomto počítači (13. 10. 2026)

- VS Code 1.141.0; rozšíření TerosHDL 7.0.3.
- OSS CAD Suite 20261013, Windows x64, v
  C:\Users\alesb\AppData\Local\Programs\oss-cad-suite.
  Stažený archiv má SHA-256
  d1bad77b4c2323275a657a385479ca82f1c4a4b3eab80f77a7bfd19f16433cdf.
- Yosys 0.69+286; nextpnr-ice40 0.11.1-59-g837d0eed.
- Icarus Verilog / vvp 14.0 devel (s20260301-578-gbc0248e6a-dirty).
- Verilator 5.053 devel (v5.052-385-gb8e7f1e4b).
- icepack a icetime jsou součástí OSS CAD Suite 20261013;
  jejich spustitelné soubory nemají samostatný přepínač verze.
  Oba byly skutečně spuštěny.

Zdroj nástrojů: [YosysHQ OSS CAD Suite](https://github.com/YosysHQ/oss-cad-suite-build/releases/tag/2026-10-13).
Zdroj rozšíření: [TerosHDL Marketplace](https://marketplace.visualstudio.com/items?itemName=teros-technology.teroshdl).

## Sestavení ve VS Code

Otevřít jako pracovní složku kořen DS203-HW-2.81, vyvolat
**Terminal → Run Task** a zvolit některou úlohu FPGA W1.2-test.
Ctrl+Shift+B spustí Vše: čtyři simulace, Verilator lint, Yosys
syntézu, nextpnr place & route, icepack a icetime. Úlohy jsou v
[.vscode/tasks.json](../.vscode/tasks.json).

Přímo v PowerShellu lze spustit například:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\FPGAW1.2-test\build.ps1 -Step all

Parametr -Step přijímá sim, lint, synth, pnr, timing, bitstream
nebo all. Skript sám aktivuje environment.ps1 z OSS CAD Suite.
Pro jiné umístění suite nastavte proměnnou prostředí
OSS_CAD_SUITE_ROOT na její kořen. Úlohy nepotřebují správce.
Všechny výstupy i logy se ukládají pouze do ignorované složky
build/FPGAW1.2-test/. Skript nemá žádný příkaz pro programování
přístroje.

## Výsledky sestavení 14. 10. 2026

| Kontrola | Výsledek |
| --- | --- |
| tb_io_ctrl | PASS, včetně protokolu 0101 a značky W1.2 0102 |
| tb_clock_phase | PASS, stejná a opačná fáze hodin ADC |
| tb_interlace | PASS, 32 známých dvojic v obou režimech, kontrola X/Z |
| tb_capture_window | PASS, dva záznamy; 512 nových dvojic po zahození 152 úvodních |
| Verilator lint | Dokončen s upozorněními ve zděděném HDL |
| Yosys synth_ice40 | Dokončeno |
| nextpnr HX4K / CB132 / 72 MHz / seed 3 | Dokončeno |
| icepack | 135 100 B; výstup shodný s W12TEST.BIN v balíčku |
| icetime -c 72 | PASS, nejdelší cesta 13,14 ns, odhad 76,12 MHz |

Úplné `build.ps1 -Step all` skutečně prošlo. Výchozí seed je po souhlasu
uživatele 3. Seed určuje rozmístění stejných obvodů; nemění funkce ani
piny. Označený návrh se seedem 1 měl 14,04 ns a se seedem 2 měl 13,92 ns;
oba neprošly limitem 13,89 ns. Předchozí naše sestavení bez značky mělo
13,22 ns. Tyto odhady nejsou změřeným zpožděním původního autorského W1.1
ani celkovou latencí od analogového vstupu.

Time.sdc zůstává referencí; otevřený build nastavuje 72 MHz přes nextpnr
--freq a icetime -c. Varování Yosys/Verilator o tri-state, šířkách a
neúplných case nebyla automaticky označena za opravená.

## Kruhový index a platné okno

Výraz `TrigBufAddr-2` při indexu 0/1 vracel v simulaci neplatná data.
Experiment používá `TrigBufAddr-2'b10`, tedy odečítání modulo 4.
Chyba původního skutečného W1.1 tím prokázána není; syntéza mohla již
původně vytvořit zamýšlený obvod. Testy nyní odmítají X/Z, inicializují
model a čtou platné okno. Regresní model našel 146 starých dvojic v úvodu;
po zahození 152 bylo dalších 512 dvojic nových. Nejde o univerzální
změřenou konstantu každého zařízení.

## Hardware a 144 MSa/s

Prokládání existovalo již ve W1.1. Uživatel na HW 2.82 ověřil dřívější
baseline přes FPGALOAD a obnovu W1.1. INT144 v3 následně při GEN→A a
1/2/4 MHz potvrdil na původním W1.1 podle postupu posun půl periody,
pořadí B→A a relativní DC korekci v RAM. Podrobnosti jsou v
[INT144.md](../QuadPawn/Programs/INT144.md). Výsledky se automaticky
nepřenášejí na označený W1.2. Absolutní přesnost, celková analogová
kvalita a všechny triggery ověřeny nejsou. Simulace má ideální ADC model
s 1ns zpožděním, nikoli pipeline skutečného ADC.

Na zařízení se nic automaticky nenahrává a trvalý zápis FPGA do flash
se netestoval. Pinové mapování z W1.1 pro HW 2.81 a novější přijal
nextpnr; přijetí nástrojem nenahrazuje kontrolu konkrétní desky.

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
