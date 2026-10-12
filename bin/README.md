# Finální obrazy DS203

Tento adresář obsahuje výstupy určené k nahrání do zařízení. Mezivýsledky
kompilace (`.o`, `.d`, `.elf`, `.map`) zůstávají v ignorovaných adresářích
`build/`. Původní obrazy k porovnání jsou v [`reference_builds/`](../reference_builds/README.md).

| Součást | Cesta | Význam |
| --- | --- | --- |
| Wildcat V6.5b | `Wildcat/` | `app1.hex` nebo tři dvojice `APP1A/B/C.ADR` + `.BIN` pro oddělené oblasti flash |
| SYS 1.64 + AlterBIOS | `SYS164AB/` | `SYS164AB.ADR` + `.BIN`; též `.HEX` |
| QuadPawn | `QuadPawn/` | `PAWN4A.ADR` + `.BIN`; též `PAWN4.HEX`; programy v `Programs/` |
| FPGA W1.1 pro Wildcat | `FPGAW1_1/` | `FPGAW1_1.ADR` + `.BIN` |

Každý `.ADR` má stejně pojmenovaný `.BIN` a základ názvu nejvýše 8 znaků.
QuadPawn je samostatný runtime. Soubor FPGA je čtvrtým obrazem `.BIN` v celém
adresáři `bin/`, nikoli čtvrtou částí Wildcatu. Před nahráním ověřte typ
hardwaru a přesné adresy podle [dokumentace sestavení](../BUILDING.md).
