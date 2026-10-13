# FPGATest pro DS203 HW 2.81/2.82

Postup a výsledky: [FPGALOAD.md](../../QuadPawn/Programs/FPGALOAD.md).
Samostatný test prokládání s interním generátorem: [INT144.md](../../QuadPawn/Programs/INT144.md).

- runtime/: QuadPawn verze 2026.10.13 pro slot 4; zvolte HEX nebo
  odpovídající ADR/BIN pár pro instalaci přes DFU.
- disk/: FPGALOAD.AMX, INT144.AMX a W12TEST.BIN na běžný interní disk.
- Původní funkční runtime zůstal v ../QuadPawn/.

Na PC prošly kompilace a 16 regresních kontrol loaderu.
INT144 v3 se přeložil bez chyb a varování; 40 numerických případů prošlo v interpretu Pawn na PC. V3 po nastavení čtecího ukazatele zahodí 152 úvodních dvojic a vyhodnocuje dalších 512. V2 při 1/2/4 MHz naměřila posun S=0, I=−4 a Delta=−4, ale mohla obsahovat stará předtriggerová data; její DC korekce a pořadí B→A se musí znovu ověřit v3. Běh v3 na zařízení zatím neproběhl. Úplné výsledky a výhrady jsou v INT144.md.
Po opravě kruhového indexu pouze v experimentálním HDL prošly čtyři FPGA simulace s kontrolou neznámých hodnot. Nový tb_capture_window prokázal 146 starých dvojic v úvodu každého ze dvou záznamů a 512 čerstvých dvojic po zahození 152. Nový bitstream je pouze v build/; zdejší W12TEST.BIN zůstává dosavadní, vyzkoušená baseline.
Uživatel potvrdil na DS203 HW 2.82 spuštění runtime PAWN_TST.HEX ze slotu 4 po nahrání na jiném počítači, spouštění .AMX z interního disku, dočasné načtení W12TEST.BIN aplikací FPGALOAD, úspěšné testy ID/FIFO při 1 MHz a 72 MHz a návrat k W1.1. FPGALOAD samo režim 144 MSa/s netestuje; následný samostatný test INT144 v2 je popsán výše. Celková analogová kvalita změřena nebyla.
Instalace runtime do slotu 4 je oddělená od následného dočasného
načítání bitstreamu do FPGA aplikací FPGALOAD.
