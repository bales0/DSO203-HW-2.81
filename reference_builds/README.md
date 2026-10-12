# Referenční obrazy pro DS203 HW 2.81/2.82

Tyto obrazy zachovávají přesné adresy převzatých projektů. Wildcat a FPGA
jsou kopie originálních buildů; SYS a QuadPawn byly sestaveny samostatně
z převzatých zdrojů. Každou změnu firmware porovnávejte s odpovídajícím
obrazem a linker skriptem.

| Součást | Soubor | Původ / kontrola | Umístění ve flash |
| --- | --- | --- | --- |
| Wildcat V6.5b | `Wildcat/app1.hex` | [Turro75/DS203-DSOQuad, revize `60614c5`](https://github.com/Turro75/DS203-DSOQuad/blob/60614c5b071cfc63fcf339e237f63e9a991e3bbf/WildcatV6.5/app1.hex), SHA-256 `1843ba16d4132c66e8be918f490def3e9ff7bbc97d1d651f216d00a97c8c2bcb` | Intel HEX: `0x0800C000–0x0801BD4F`, `0x0802C000–0x0803C7B3`, `0x08047000–0x08057183` |
| SYS 1.64 + AlterBIOS | `SYS/SYS164AB.ADR`, `SYS/SYS164AB.BIN`, `SYS/SYS164AB.HEX`, `SYS/SYS164AB.elf`, `SYS/SYS164AB.map` | Sestaveno místně oficiálním Arm GCC 9.2.1; BIN je po bajtech totožný s [autorovým vydáním v1.0](https://github.com/Turro75/DSO203_SYS164_GCC/releases/tag/v1.0), SHA-256 `480ea031f146906a885510e4b88c6569516e85feeb544443904ff1d98ab1c3de` | `.ADR`: `0x08004000`; 26 303 bajtů, konec `0x0800A6BE`; tabulka AlterBIOS `0x08004400` |
| QuadPawn | `QuadPawn/PAWN4A.ADR`, `QuadPawn/PAWN4A.BIN`, `QuadPawn/PAWN4.HEX`, `QuadPawn/PAWN4.elf`, `QuadPawn/PAWN4.map` | Sestaveno místně z [Turro75/QuadPawn, revize `f702d57`](https://github.com/Turro75/QuadPawn/tree/f702d570c8cafcd94cbbf90a4eba080a5d1d3784) s Arm GCC 9.2.1; SHA-256 BIN `307249a221e9a762a5232bedd1b32495c2e82415d21e201da92c0fac11ad2621` | `.ADR`: `0x08024000`; 29 940 bajtů, konec `0x0802B4F3`, uvnitř 32KiB slotu 4 |
| FPGA W1.1 | `FPGA/FPGAW1_1.ADR`, `FPGA/FPGAW1_1.BIN` | [MotoMaxis/DS203-DSOQuad, revize `ef08bdb`](https://github.com/MotoMaxis/DS203-DSOQuad/tree/ef08bdb195742fb88077a59bb898916aeef7ba5f/WildcatV6.5/W1.1%20FPGA), SHA-256 BIN `bf86d13b51bee7fce5d5f82906ea9086091fbee66a52d08622245d4838279739` | `.ADR`: `0x0805C800`; 135 096 bajtů, konec `0x0807D7B7` |
| LOGO pro HW 2.81/2.82 | `LOGO/LOGO.ADR`, `LOGO/LOGO.BIN`, `LOGO/LOGO.BMP` | Z místního původního balíku `recovery/RECOVERY281282.zip`; `LOGO.BMP` je bezeztrátově vyňatý BMP z BIN, podrobnosti v [LOGO/README.md](LOGO/README.md) | `.ADR`: `0x0807D800`; 10 240 bajtů, konec `0x0807FFFF` |

Převzatá větev QuadPawn neobsahovala hotový `PAWN4` runtime; referenční obraz
je místní sestavení bez úprav zdrojů. `Runtime/linker_scripts/app4.lds`
přiřazuje slotu 4 rozsah `0x08024000–0x0802BFFF` (32 KiB) a
`Runtime/linker_scripts/main.lds` očekává tabulku AlterBIOS na `0x08004400`.
Původní Makefile vytváří `PAWN4.HEX` bez koncového EOF záznamu. Pro kontrolu
adres slouží ELF a mapa; pro instalaci tvoří dvojici `PAWN4A.ADR/BIN`.

Referenční SYS HEX má stejný obsah jako BIN na všech obsazených adresách;
jediný výplňový bajt na `0x0800447F` je pouze v BIN. Mapa a ELF patří ke
stejnému místnímu sestavení a umožňují kontrolu adres jednotlivých symbolů.

Původní FPGA soubor `.ADR` se jmenoval `FPGA_281.ADR` a BIN `281_FPGA.BIN`.
V referenční i zdrojové složce mají shodný osmiznakový základ `FPGAW1_1`.
`SYS164AB` znamená SYS 1.64 s integrovaným AlterBIOS. Zdrojový kód a
binární obsahy se přejmenováním nezměnily.

Wildcat HEX je originální soubor z převzaté revize. Místní sestavení
`bin/Wildcat/app1.hex` používá stejná počáteční umístění oblastí, ale má jiný
binární obsah i délky. Referenční obraz proto nepřepisujte místním buildem.

Finální výstupy jsou v kořenovém `bin/` po jednotlivých součástech.
Mezisoubory sestavení zůstávají v ignorovaných adresářích `build/`.
Složka `QuadPawn/Programs/` zde navíc uchovává 18 původních `.amx` programů.
Nové sestavení QuadPawn může mít jiný SHA-256 kvůli vloženému času kompilace;
v ověřeném sestavení se změnilo jen 15 bajtů v časových řetězcích.
