# Finální obrazy DS203

Tento adresář obsahuje výstupy určené k nahrání do zařízení. Mezivýsledky
kompilace (`.o`, `.d`, `.elf`, `.map`) zůstávají v ignorovaných adresářích
`build/`. Původní obrazy k porovnání jsou v [`reference_builds/`](../reference_builds/README.md).

| Součást | Cesta | Význam |
| --- | --- | --- |
| Wildcat V6.6 — nový režim 144 | `WildcatV6.6/` | [Ovládání a PC výsledky](../WildcatV6.6/README.md); app1.hex a tři ADR/BIN páry |
| FPGA W1.2 — výchozí připravená instalace | `FPGAW1_2/` | [Instalační obraz a postup](FPGAW1_2/README.md); stejná oblast jako W1.1 |
| QuadPawn 2026.10.14.2 pro W1.1/W1.2 | `QuadPawnW12/` | [Slot 4 a FPGALOAD](QuadPawnW12/README.md); nový runtime před výměnou uloženého FPGA |
| Wildcat V6.5b | `Wildcat/` | `app1.hex` nebo tři dvojice `APP1A/B/C.ADR` + `.BIN` pro oddělené oblasti flash |
| SYS 1.64 + AlterBIOS | `SYS164AB/` | `SYS164AB.ADR` + `.BIN`; též `.HEX` |
| QuadPawn | `QuadPawn/` | `PAWN4A.ADR` + `.BIN`; též `PAWN4.HEX`; programy v `Programs/` |
| FPGA W1.1 pro Wildcat | `FPGAW1_1/` | `FPGAW1_1.ADR` + `.BIN` |

Každý `.ADR` má stejně pojmenovaný `.BIN` a základ názvu nejvýše 8 znaků.
QuadPawn je samostatný runtime. FPGA je samostatný obraz;
Wildcat se nadále skládá ze tří oddělených částí APP1A/B/C. Před nahráním ověřte typ
hardwaru a přesné adresy podle [dokumentace sestavení](../BUILDING.md).

Nové položky 144D/144A a fyzické uložení společného WPT ještě nejsou
na DS203 ověřené. Uživatel potvrdil instalaci W1.2 přes ADR/BIN a běh
předchozího Wildcatu. SYS/DFU ani původní
referenční obrazy se tímto vývojem neměnily.
