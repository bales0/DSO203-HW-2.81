# FPGA W1.2-test — vývojová větev pro DS203 HW 2.81/2.82

Experimentální pracovní kopie [W1.1 FPGA](../W1.1%20FPGA/README.TXT) pro
Lattice iCE40HX4K-CB132. Čtyři HDL soubory a Time.sdc jsou po bajtech
shodné s W1.1. V DP_RAM.v jsou tři kruhové indexy opravené na
dvoubitové odečítání. Pins.pcf zachovává všech 45 původních fyzických pinů;
pouze zápis vektorových názvů DB[n] a Din[n] nemá zpětná lomítka,
aby jej přijal nextpnr. Původní W1.1 se neměnila.

Toto je vývojové prostředí, nikoli hotová funkční verze W1.2.
Firmware stále vrací ID W1, podverzi 0x0101. Vygenerovaný
build/FPGAW1.2-test/FIFO.bin je nový experimentální výstup po opravě
kruhového indexu. Tento obraz na zařízení testován nebyl. Dřívější
baseline W12TEST.BIN uživatel na HW 2.82 úspěšně načetl přes FPGALOAD,
provedl testy ID/FIFO a obnovil W1.1. Balíček bin/FPGATest/disk/W12TEST.BIN
zůstává původní (SHA-256 c979877f801e4371e4d5117d72549856a7bc573dac867aa8f2457277f72ffe16).
Není tu instalační pár ADR/BIN ani ověření analogové kvality nového obrazu.

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

## Výsledky na tomto počítači

| Kontrola | Výsledek |
| --- | --- |
| tb_io_ctrl (sériové registry, 32bitový čas triggeru, W1 ID, datová sběrnice) | PASS |
| tb_clock_phase (CKA/CKB ve stejné a opačné fázi) | PASS |
| tb_interlace (32 známých párů v obou režimech, po zahození 152 úvodních dvojic) | PASS, explicitní kontrola neznámých hodnot |
| tb_capture_window (dva různé záznamy za sebou) | PASS; 146 starých dvojic v každém úvodu; po zahození 152 dvojic všech 512 ponechaných dvojic nových |
| Verilator lint | Dokončen s upozorněními ve zděděném HDL, bez chyb |
| Yosys synth_ice40 -top FIFO | Dokončeno, kontrolní fáze 0 problémů |
| nextpnr --hx4k --package cb132 --freq 72 --seed 1 | Dokončeno, všech 45 pinů přiřazeno |
| icepack | Vygeneroval nový FIFO.bin, 135 100 B, SHA-256 5fb620f171ab6a769f38afe6412af5de0c9c781875b5ab56df7d912031ed0896 |
| icetime -d hx4k -P cb132 -c 72 | PASS; nejdelší cesta 13,22 ns (odhad 75,67 MHz), požadavek 13,89 ns |

nextpnr po směrování hlásí pro MCI odhad 83,90 MHz; odhad
icetime 75,67 MHz je konzervativnější. Jde o dvě odlišné časové
metody, nikoli měření na zařízení. Převzaté Time.sdc je ponecháno
jako reference; tento otevřený build nastavuje cíl 72 MHz přímo
přepínačem --freq a kontrolou icetime -c 72.

Yosys upozornil na omezenou podporu tri-state v IO_Ctrl.v a
rozbalení malých akumulátorů na registry. Verilator hlásí mimo jiné
šířky výrazů a neúplné case v původním HDL. Žádné upozornění nebylo
automaticky označeno za opravené. Originální W1.1 bitstream má
135 096 B; nový výstup má 135 100 B, takže nejde o binárně
shodnou náhradu.

## Oprava simulace a kruhového indexu

Původní tb_interlace nekontroloval neznámé hodnoty a nevyvolal spolehlivou
hranu resetu čtecího stavu. Jeho dřívější PASS proto nebyl dostatečným
ověřením dat. Opravený test odmítá X/Z, explicitně modeluje známý počáteční
stav nezresetovaných čítačů a skutečnou hranu resetu a čte platné okno.

Přísná kontrola odhalila, že TrigBufAddr-2 při 0/1 v simulaci čte mimo
čtyřprvkový buffer. Pouze v DP_RAM.v této experimentální kopie se nyní
používá TrigBufAddr-2'b10, tedy výslovné modulo 4. HDL W1.1 se nezměnilo.
Po této opravě prošly všechny čtyři simulace, lint s původními upozorněními,
syntéza, place & route, icepack a časová kontrola na 72 MHz.
Nový bitstream má jiný hash než dřívější baseline a nesmí se zaměňovat
za již vyzkoušený W12TEST.BIN. Testovací balíček ani CRC v aplikacích
se touto změnou neaktualizovaly. Číslo 146 je výsledek konkrétního
regresního modelu; zahození 152 dvojic ověřilo jeho čisté okno.

## 144 MSa/s a hardware

Převzatý W1.1 obsahuje přepnutí hodin ADC B do opačné fáze a ukládá
vzorky A/B společně do jednoho 16bitového slova při každém taktu MCI.
SYS podporuje režim INTERLACE přes GPIO PC3 a QuadPawn jej umí zapnout. Model ADC v testbenchi používá jen ideální výstupní zpoždění 1 ns; neobsahuje skutečnou pipeline ani režim data-align fyzického ADC. Znaménko jeho časového pořadí tedy nelze přenášet na hardware. Výsledky INT144 v2 na zařízení jsou předběžné kvůli úvodní historii a musí je ověřit INT144 v3.
Nový tb_interlace se syntetickým společným vstupem ověřil 32 po sobě
jdoucích dvojic ve správném pořadí a 32 dvojic po návratu do odděleného
režimu. To potvrzuje možnost prokládané datové cesty v tomto HDL,
nikoli skutečnou rychlost 144 MSa/s na DS203.

Uživatel na DS203 ověřil dočasné načtení dřívější baseline, základní
testy ID/FIFO a obnovu W1.1. FPGALOAD nezapíná INTERLACE. Zatím
nebylo změřeno směrování stejného analogového vstupu, časování ADC,
pořadí skutečných vzorků, analogová kvalita, všechny režimy triggeru
ani kalibrace pro 144 MSa/s. Trvalé ukládání FPGA do flash se netestovalo.

Původní mapování pochází z W1.1 pro HW 2.81 a novější. Kopie má
45 různých přiřazení a nextpnr je pro iCE40HX4K-CB132 přijal.
To ověřuje syntaxi a obsazení pinů v nástroji; nenahrazuje měření
na konkrétní revizi desky.
