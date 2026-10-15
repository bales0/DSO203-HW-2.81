# QuadPawn 2026.10.14 — místní runtime pro slot 4

Výstupy PAWN4T.HEX a PAWN4TA.ADR/BIN. Tento runtime se sestavuje odděleně
od původního `bin/QuadPawn/` i staršího balíčku `bin/FPGATest/runtime/`.
Tyto jiné kopie se tímto krokem neaktualizují.

About nyní obsahuje místní verzi 2026.10.14, Base: Turro75 f702d57,
SYS GCCA0 / AlterBIOS merged, Local: checked FPGA loader, projektové Git ID,
datum sestavení a GCC. Projektové Git ID není upstreamová verze QuadPawn.
VERSION lze při sestavení změnit. Aktualizace About nemění Pawn API ani
nepřidává detekci FPGA do runtime; profily čtou jednotlivé Pawn aplikace.

Ověřeno 14. 10. 2026 pomocí aktuálních GitHub refs a commit API:

- [Turro75/sysgcc_mergedalterbios](https://github.com/Turro75/QuadPawn/tree/f702d570c8cafcd94cbbf90a4eba080a5d1d3784)
  stále končí f702d57 z 20. 9. 2022 (fix alterbios_check).
- [Původní PetteriAimonen/master](https://github.com/PetteriAimonen/QuadPawn/tree/704ea78c61738574bb62407f503fc49a13ffb405)
  stále končí 704ea78 z 3. 7. 2021. Větev Turro75 je o dva commity vpředu,
  bez chybějících upstreamových commitů.
- bales0/sysgcc_mergedalterbios odpovídá f702d57. Větev fpga_loader
  končí starým commitem 9c3606e z 10. 11. 2012, není novějším základem.

Jde o aktuální stav těchto ověřených větví, nikoli důkaz kontroly všech
forků na internetu. Místní opravy loaderu jsou nad touto základnou.

Sestavení (GCC 9.2.1 a GNU Make v PATH):

```text
make -C QuadPawn/Runtime NAME=PAWN4T BINDIR=../../bin/QuadPawnTest VERSION=2026.10.14
```

Překlad a link prošly; slot 4 začíná na 0x08024000 a má 32 KiB.
Nový BIN má 30 648 B, vejde se do slotu. HEX obsahuje standardní EOF.
Na DS203 se nové sestavení zatím netestovalo a nic se automaticky nenahrálo.
Provenience v About je deklarace při sestavení, nikoli automatické rozpoznání
libovolného starého internetového HEX.
