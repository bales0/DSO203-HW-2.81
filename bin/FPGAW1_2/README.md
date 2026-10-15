# Instalační FPGA W1.2 pro DS203 HW 2.81/2.82

Připravený trvalý nástupce W1.1; instalace na zařízení zatím neproběhla.
SYS GCCA0 + AlterBIOS se nemění. Instaluje se konfigurace FPGA do stejné
MCU flash oblasti jako původní W1.1; při startu ji načítá existující systém.

- `FPGAW1_2.ADR`: 0x0805C800.
- `FPGAW1_2.BIN`: 135 096 B, končí na 0x0807D7B7; CRC-32 66B80BD2.
- `FPGAW1_2.HEX`: stejné bajty/adresy, 16B datové záznamy, kontrolní součty a EOF.
- SHA-256 BIN: `cd4b62edcb4e673301fa7433958e9445c3cc9eb0a301d9e2325fc103aa911c7f`.

Zdroj je označený W1.2 ze `bin/FPGATest/disk/W12TEST.BIN`, SHA-256
`787fff5254366c370ed571da5a5c606986160806a6e1e99164718aa5ef381ddb`.
Konverze odstraní pouze čtyřbajtový úvod `FF 00 00 FF`. Instalační obraz
začíná synchronizačním slovem `7E AA 99 7E`, stejně jako W1.1. Délka i
adresy odpovídají původnímu páru. Ověřuje je test_system_package.py.

Sestavení offline: `python -B FPGAW1.2-test/export-system.py`. Neznámý
nebo změněný vstupní hash se odmítne; skript neprogramuje zařízení.

## Ruční instalace po samostatném souhlasu

1. Na ověřeném PC přes DFU nejdřív nahrát nový slot-4 runtime
   `../QuadPawnW12/PAWN4W.HEX` a ověřit `.RDY`, spuštění s K4 a About
   2026.10.14.2. S původním W1.1 tento runtime funguje také.
2. Nahrát nový `../WildcatV6.6/app1.hex`, ověřit `.RDY` a běžný start.
3. Přes DFU zkopírovat pouze `FPGAW1_2.ADR`, počkat na znovupřipojení,
   pak `FPGAW1_2.BIN` a počkat na `.RDY`. Alternativou je pouze
   `FPGAW1_2.HEX`; obě metody současně nekombinovat. Při `.ERR` nepokračovat.
4. Po běžném zapnutí musí Wildcat zobrazit FPGA W1.2. Nejprve ověřit
   běžné dvoukanálové měření, potom položku 144D/144A v menu vazby A
   podle [návodu](../../WildcatV6.6/README.md). Původní kombinace
   K3+K4 a K1+K4 nejsou v aktuálním Wildcatu dostupné.
5. Na běžný interní disk patří nový `../QuadPawnW12/FPGALOAD.AMX`.
   FPGALOAD má při Test/Restore rozpoznat uloženou W1.2. Starý FPGALOAD
   i starý runtime s pevnou kontrolou W1.1 pro tento krok nepoužívat.

Obnova W1.1 používá zachovaný `../FPGAW1_1/FPGAW1_1.ADR` + `.BIN`.
Nový runtime přijímá obě varianty. S W1.1 bude nová běžná 144 smyčka
zablokovaná, původní diagnostika a běžný Wildcat zůstanou dostupné.
SYS, DFU, LOGO, WPT/CFG a jiná kalibrační data nejsou součástí tohoto obrazu.
