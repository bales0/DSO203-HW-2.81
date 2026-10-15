# FPGATest pro DS203 HW 2.81/2.82

Pro trvale instalované W1.2 použít nové [QuadPawnW12](../QuadPawnW12/README.md)
a jeho FPGALOAD.AMX. Tento zachovaný starší balíček testuje uložené W1.1;
nový raw instalační obraz W1.2 je v [FPGAW1_2](../FPGAW1_2/README.md).


Postup a výsledky: [FPGALOAD.md](../../QuadPawn/Programs/FPGALOAD.md).
Samostatný test prokládání s interním generátorem: [INT144.md](../../QuadPawn/Programs/INT144.md).

- runtime/: QuadPawn verze 2026.10.13 pro slot 4; zvolte HEX nebo
  odpovídající ADR/BIN pár pro instalaci přes DFU.
- disk/: FPGALOAD.AMX, INT144.AMX a W12TEST.BIN na běžný interní disk.
- Původní funkční runtime zůstal v ../QuadPawn/.

Na PC prošly kompilace a 16 regresních kontrol loaderu.
INT144 v3 se přeložil bez chyb a varování; 40 numerických případů prošlo v interpretu Pawn na PC. V3 zahodí 152 úvodních dvojic a vyhodnocuje dalších 512. Uživatel 14. 10. 2026 na HW 2.82 při 1/2/4 MHz naměřil vždy S=0, I=−4 a Delta=−4 bez ořezání. Čistý test ověřil prokládání při nominálních 144 MSa/s a pořadí B→A. DC korekce Q10 jsou S=1078/−3514 a I=1052/−885; zůstávají v RAM. Podle postupu byl aktivní původní W1.1; INT144 bitstream nenačítá. Absolutní přesnost zůstává neměřená. Výsledky v2 zůstávají historické s výhradou starých vzorků. Úplná tabulka je v INT144.md.
Po opravě kruhového indexu pouze v experimentálním HDL prošly čtyři FPGA simulace s kontrolou neznámých hodnot. Nový tb_capture_window prokázal 146 starých dvojic v úvodu každého ze dvou záznamů a 512 čerstvých dvojic po zahození 152. Zdejší W12TEST.BIN nyní obsahuje označené W1.2, CRC 33AFAD10, SHA-256 787fff5254366c370ed571da5a5c606986160806a6e1e99164718aa5ef381ddb. Předchozí ověřená baseline je v reference/W12BASE.BIN. Nový obraz dosud na zařízení netestován.
Uživatel potvrdil na DS203 HW 2.82 spuštění runtime PAWN_TST.HEX ze slotu 4 po nahrání na jiném počítači, spouštění .AMX z interního disku, dočasné načtení W12TEST.BIN aplikací FPGALOAD, úspěšné testy ID/FIFO při 1 MHz a 72 MHz a návrat k W1.1. FPGALOAD samo režim 144 MSa/s netestuje; následný samostatný test INT144 v3 je popsán výše. Celková analogová kvalita změřena nebyla.
Instalace runtime do slotu 4 je oddělená od následného dočasného
načítání bitstreamu do FPGA aplikací FPGALOAD.

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
