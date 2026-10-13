# FPGALOAD: test dočasného načtení W1.2-test

Balíček: [bin/FPGATest](../../bin/FPGATest/README.md).
Zdroj: [FPGALOAD.pawn](FPGALOAD.pawn).
Ověřená aplikace FPGAW12 a původní hotový runtime v bin/QuadPawn se neměnily.

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

Verze runtime je volitelná parametrem VERSION; výchozí je 2026.10.13.
Zobrazuje se po stisku About v prohlížeči QuadPawn. Změna parametru vynutí
překlad souboru s označením verze i bez clean. Na PC byly ověřeny verze
1.2 a následně 2026.10.13.

Z kořene projektu s GCC 9.2.1, GNU Make a shellovými nástroji v PATH:

    make -C QuadPawn/Runtime NAME=PAWN4T BINDIR=../../bin/QuadPawnTest VERSION=2026.10.13

Na tomto Windows použijte C:/MinGW/bin/mingw32-make.exe a přidejte do PATH
GCC 9.2.1 a C:/msys64/usr/bin. Používejte oddělený BINDIR z příkazu výše;
běžné make bez BINDIR by přepsalo výstupy v bin/QuadPawn.

Aplikace, z QuadPawn/Programs:

    & ..\Compiler\bin\pawncc.exe 'FPGALOAD.pawn' '-d2' '-v2' '-V1' '-i..\Compiler\include' '-o..\..\bin\QuadPawn\Programs\FPGALOAD'

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

W12TEST.BIN: 135 100 B, CRC-32 46EDF657, SHA-256
c979877f801e4371e4d5117d72549856a7bc573dac867aa8f2457277f72ffe16.
Současné W1.2-test má stejné ID a podverzi jako W1.1. Samotné ID tedy
obrazy nerozliší; přenos a přechod CDONE jsou samostatnou kontrolou.
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
Analogové směrování, kalibrace a časování 144 MSa/s na DS203 změřeny nebyly.
