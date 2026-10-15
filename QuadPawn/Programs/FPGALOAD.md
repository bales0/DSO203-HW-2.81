# FPGALOAD: test dočasného načtení W1.2

Aktuální zdroj používá nové funkce runtime 2026.10.14.2 a podporuje
obnovu známé uložené W1.1 **i W1.2**. Odpovídající AMX a runtime jsou
v [bin/QuadPawnW12](../../bin/QuadPawnW12/README.md); prošlo 22 PC scénářů.
Po obnově se očekávaná značka vybírá podle ověřeného ROM CRC, nikoli
napevno W1.1. Starší balíček FPGATest níže zůstává historickou variantou
pro uložené W1.1. Trvalou výměnu FPGA popisuje
[nový instalační návod](../../bin/FPGAW1_2/README.md).


Balíček: [bin/FPGATest](../../bin/FPGATest/README.md).
Zdroj: [FPGALOAD.pawn](FPGALOAD.pawn).
Pawn aplikace nyní používají společné profily FPGA; původní hotový runtime se nemění.

## Stav ověření

Uživatel dne 13. 10. 2026 potvrdil na DS203 HW 2.82 s W1.1 úspěch
testů FPGAW12: ID, FIFO 1 MHz a FIFO 72 MHz bez chyby a timeoutu.
Uživatel potvrdil spuštění nového runtime PAWN_TST.HEX ze slotu 4 po úspěšném nahrání na jiném počítači; soubory .AMX z běžného interního disku se spouštějí. Ve FPGALOAD potvrdil Load W1.2-test, úspěšné testy a návrat k W1.1. Diagnostika zahrnovala ID a FIFO při 1 MHz a 72 MHz. Na prvním počítači DFU V3.46 vracelo pro stejný obraz .ERR; samotný obraz proto není prokázanou příčinou této chyby.

Na PC prošlo 16 regresních kontrol skutečného loaderu s náhradním SPI/GPIO
a souborovým API: CRC zálohy a souboru, nesprávná délka, chyby čtení před
resetem i během přenosu, bajtová shoda přenosu, oba chybové stavy CDONE,
chybějící soubor, výslovná obnova, rollback, obnova při ukončení a blokace
další aplikace při selhání obnovy. Elektrické SPI a reálné CDONE hostitelský
test neověřuje.

Runtime byl sestaven GCC 9.2.1. BIN má 30 564 B v 32KiB aplikačním slotu 4
od 0x08024000 do 0x0802B763. HEX má EOF záznam. FPGALOAD se zkompiloval
Pawn compilerem 4.0.4611 bez chyb a varování.

## Verze a sestavení

Verze runtime je volitelná parametrem VERSION; aktuální výchozí je 2026.10.14.
Nový runtime s původem zdrojů v About je v bin/QuadPawnTest/; dříve ověřená kopie runtime v bin/FPGATest/runtime/ zůstává 2026.10.13.
Následující historický postup balíčku používá tuto starší ověřenou kopii.
Zobrazuje se po stisku About v prohlížeči QuadPawn. Změna parametru vynutí
překlad souboru s označením verze i bez clean. Na PC byly ověřeny verze
1.2 a následně 2026.10.13.

Z kořene projektu s GCC 9.2.1, GNU Make a shellovými nástroji v PATH:

    make -C QuadPawn/Runtime NAME=PAWN4T BINDIR=../../bin/QuadPawnTest VERSION=2026.10.13

Na tomto Windows použijte C:/MinGW/bin/mingw32-make.exe a přidejte do PATH
GCC 9.2.1 a C:/msys64/usr/bin. Používejte oddělený BINDIR z příkazu výše;
běžné make bez BINDIR by přepsalo výstupy v bin/QuadPawn.

Aplikace, z QuadPawn/Programs:

    & ..\Compiler\bin\pawncc.exe 'FPGALOAD.pawn' '-d2' '-v2' '-V1' '-i..\Compiler\include' '-o..\..\bin\QuadPawn\Programs\FPGALOAD.amx'

Regresní testy z kořene projektu:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/fpga_loader/run.ps1

## Ruční test na DS203

1. Zachovejte původní runtime v bin/QuadPawn a zálohu vlastních souborů.
   Instalace nového runtime zapíše aplikaci ve slotu 4; jde o samostatný
   krok před dočasným načítáním FPGA, bez aktualizace SYS nebo kalibrace.
2. V DFU režimu nahrajte pouze runtime/PAWN4T.HEX z balíčku. Alternativa:
   PAWN4TA.ADR, počkat na znovupřipojení, poté PAWN4TA.BIN. Zvolte
   jeden postup. Adresa je 0x08024000.
3. Restartujte do QuadPawn a přes About ověřte QuadPawn 2026.10.13.
   Nejprve zopakujte ověřené testy ID/1MHz/72MHz v FPGAW12.
4. Na běžný interní disk zkopírujte FPGALOAD.AMX a W12TEST.BIN ze složky
   disk/ balíčku. Tyto dva soubory nepatří na DFU disk.
5. Spusťte FPGALOAD a zvolte Run. Vyžaduje nové nativní funkce
   fpga_load_checked a fpga_restore_ready; starý runtime jej odmítne
   před spuštěním.
6. Zvolte Load a potvrďte Load. Program před resetem FPGA ověří velikost,
   CRC bitstreamu a záložní W1.1 v ROM. Po úspěšném CDONE zvolte
   Test a postupně ID, 1MHz, 72MHz.
7. Zvolte W1.1, zopakujte ID/1MHz/72MHz a potom Exit.
   Runtime obnovuje W1.1 i při běžném ukončení nebo chybě Pawn.
8. Při selhání obnovy vypněte a znovu zapněte DS203. Návrat na původní
   runtime je možný pomocí zachovaného PAWN4A.ADR/BIN z bin/QuadPawn.

Na zařízení test provedl uživatel. Tento postup sám do DS203 nic automaticky nenahrává.

## Kontrolní údaje a omezení

Záloha musí přesně odpovídat referenční W1.1 ve flash:
0x0805C800, 135 096 B, CRC-32 9FB87F03.
Pokud obsah nesouhlasí, nové načtení se zablokuje před resetem.
Pouhé dočasné načtení W1.1 do FPGA tuto podmínku nenahrazuje.

W12TEST.BIN: 135 100 B, CRC-32 33AFAD10, SHA-256
787fff5254366c370ed571da5a5c606986160806a6e1e99164718aa5ef381ddb.
Po Load se vyžaduje selector 2 = 0x0102; po obnově W1.1 se vyžaduje 0.
Předchozí ověřený obraz je v reference/W12BASE.BIN (CRC 46EDF657).
Nový loader jej záměrně nepřijme jako W1.2; očekává nový CRC a značku.
Historické hardwarové testy se týkaly předchozí baseline.

Při budoucí změně bitstreamu aktualizujte CRC v aplikaci a ověřte sestavení.

Loader pouze čte flash a dočasně konfiguruje FPGA přes SPI.
Nezapisuje jeho obraz do flash ani nemění kalibrační data. FPGALOAD mění
provozní registry, GPIO a při záznamu dočasně analogové nastavení stejně
jako FPGAW12. Takt a trigger navazující aplikace musí znovu nastavit.

Nativní DMA fpga_read zůstává původní; časový limit aplikace platí pro
čekání na Full/Start, nikoli pro vadnou samotnou DMA operaci.
Úspěšné FIFO testy neprokazují kvalitu ADC, všechny režimy triggeru
ani skutečných 144 MSa/s. Převzatý W1.1 má datovou cestu pro dvojice
prokládaných ADC vzorků; tb_interlace ověřil jejich pořadí v simulaci.
SYS a QuadPawn umějí zapnout INTERLACE, ale FPGALOAD jej nepoužívá.
Samostatný INT144 v3 ověřil na původním W1.1 relativní DC korekci a prokládání při nominálních 144 MSa/s; nikoli absolutní kalibraci ani nový W1.2.

Nový FPGALOAD.AMX má 29 532 B, odhad RAM 13 072 B.

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
