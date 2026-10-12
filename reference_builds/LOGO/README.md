# Referenční LOGO pro DS203 HW 2.81/2.82

Zdroj: `recovery/RECOVERY281282.zip`, adresář `RECOVERY281282/5.LOGO/`.
Stejné bajty LOGO jsou také v úplném obrazu flash z tohoto archivu od
offsetu `0x7D800`. Jde o referenci továrního/recovery obrazu, nikoli
o samostatně nalezený projekt s původní grafikou.

| Soubor | Obsah | Velikost | SHA-256 |
| --- | --- | ---: | --- |
| `LOGO.ADR` | ASCII adresa `0x0807D800` | 12 B | `b5cfbd1162fd9428197c8047dba491620e03f1f1be64ebb2bb32583bf10580cf` |
| `LOGO.BIN` | Instalační oblast ve flash; BMP následované výplní `0xFF` | 10 240 B | `88ec35a5d4fdb7ac719be409f259c7fe1b87cb3e950b1e0fc90cd88abede5d19` |
| `LOGO.BMP` | Upravitelný BMP, prvních 8 310 bajtů BIN | 8 310 B | `49eebd11dedeb740a84b4d8c2b6be8375541697b50e8f2c404d5d61962905162` |

BMP má 256 × 64 pixelů a 4 bity na pixel (16 barev). Původní BIN je
vyplněn do 10KiB flash oblasti `0x0807D800–0x0807FFFF`. `LOGO.BMP` je
zdroj obrázku pro úpravy; `LOGO.BIN` je přesná referenční kopie pro
HW 2.81/2.82. Změněný BMP bez přípravy správného BIN nepoužívejte jako
náhradu tohoto referenčního instalačního páru.

[Výrobce](https://forum.minidso.com/forum.php?mod=viewthread&tid=666)
potvrzuje, že HW 2.81 používá pro LOGO jiný adresní soubor než starší
revize. [Podpora Seeed](https://forum.seeedstudio.com/t/ds203-keeps-beeping-need-help/5853)
pro HW 2.82 uvádí obnovu LOGO pomocí adresního souboru a obrázku.
