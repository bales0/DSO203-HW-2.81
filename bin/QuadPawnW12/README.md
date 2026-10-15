# QuadPawn 2026.10.14.2 — uložené FPGA W1.1/W1.2, slot 4

Samostatné nové sestavení, na zařízení zatím neověřené. Zdrojová základna
zůstává Turro75 f702d57 navazující na PetteriAimonen/QuadPawn. About uvádí
místní verzi, původ, merged SYS/AlterBIOS a Local: FPGA ROM W1.1/W1.2 loader.

- `PAWN4W.HEX`: runtime, standardní EOF.
- `PAWN4WA.ADR` + `.BIN`: stejný runtime na 0x08024000, BIN 30 792 B,
  vejde se do 32 KiB slotu 4. Související ADR/BIN mají stejný základ názvu.
- `FPGALOAD.AMX`: aktualizovaný program pro tento runtime.
- `VERSION.TXT`: skutečně použitý volitelný parametr VERSION.

Loader přijímá pouze dvě známé zálohy na 0x0805C800, délka 135 096 B:
W1.1 CRC 9FB87F03 nebo instalační W1.2 CRC 66B80BD2. Neznámý obraz
načítání zablokuje. Obnova i ukončení Pawn aplikace vrací právě uloženou
variantu. Nové native funkce fpga_restore_crc/fpga_restore_version vracejí
ověřený CRC a verzi 0x0101/0x0102 (při chybě 0). FPGALOAD podle nich
ověřuje obnovu: W1.1 selector2=0, W1.2 selector2=0x0102.

Překlad ARM GCC 9.2.1 a Pawn 4.0.4611 prošel. Prošlo 22 loaderových
regresních scénářů a kontrola každého bajtu HEX/BIN/ADR včetně vektorů.
Elektrické SPI, fyzická obnova ani nový runtime nebyly tímto na HW ověřeny.
Původní bin/QuadPawn a bin/QuadPawnTest se nepřepisovaly.

Z kořene projektu:

```powershell
./QuadPawn/build-w12.ps1
./QuadPawn/build-w12.ps1 -Version 1.2
./tests/fpga_loader/run.ps1
python -B ./tests/wildcat144/test_system_package.py
```

Při instalaci HEX je použito pouze PAWN4W.HEX do DFU; při volbě BIN
nejdřív PAWN4WA.ADR, po znovupřipojení PAWN4WA.BIN. FPGALOAD.AMX patří
na běžný interní disk. [Pořadí instalace](../FPGAW1_2/README.md) začíná
novým runtime před přepsáním uloženého FPGA na W1.2.
