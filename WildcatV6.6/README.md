# Wildcat V6.6 — běžné režimy 144D / 144A

Vývoj pro DS203 HW 2.81/2.82 s 8MB diskem, 15. 10. 2026. Režim používá
oba ADC pro jediný vstup A, nominálně 144 MSa/s, pořadí B→A. Akvizice
probíhá po jednom rámci z hlavní smyčky Wildcatu, s běžnými skupinami
menu Current/Detail. Samostatná diagnostická obrazovka i oddělený průvodce kalibrace 144 byly odstraněny.
Nová integrace a společné ukládání kalibrace ještě nebyly testovány na DS203.

## Spuštění a ovládání

V běžném menu vybrat **vstup A → vazba DC/AC/DT → 144D nebo 144A**.
144D používá DC vazbu, 144A AC vazbu. Pořadí volby je DC → AC → DT →
144D → 144A → TL → RS → i2 → SP. Staré číselné hodnoty režimů v uložených
konfiguracích se nemění. Bez rozpoznaného FPGA W1.2 se položky 144 přeskočí.
Před spuštěním se navíc ověří CRC trvale instalovaného FPGA v oblasti
0x0805C800. Samotné dočasné načtení W1.2 do SRAM nestačí.

**Zkratky K1+K4 a K3+K4 jsou odstraněné.** Diagnostické programy QuadPawn
zůstávají samostatné a tato změna je neupravuje.

| Ovládání | Funkce při 144D/144A |
| --- | --- |
| Pravý přepínač ± | Výběr skupiny menu |
| K4, případně stisk pravého přepínače | Další položka uvnitř skupiny |
| Levý přepínač ± | Změna hodnoty |
| K1 | RUN/HOLD; v SGL znovu odjistí snímání |
| K2 krátce | AUTO → NORM → SGL |
| K2 dlouze, asi 3 sekundy, při vybraném A | Společná standardní kalibrace A+B+144 |
| K3 při vybrané skupině souboru | Uloží platný rámec do CSV |
| A → vazba → jiná hodnota než 144D/144A | Návrat k samostatným kanálům |

Skupina A nabízí A/−A/×10A/−×10A, vazbu, rozsah a Y pozici. Časová
základna nabízí AUTO/NORM/SGL a 0,1/0,2/0,5 µs/div. Trigger nabízí pevný
zdroj A, vzestupnou/sestupnou hranu a úroveň v µV. Kurzorové menu nastavuje
T1/T2. Souborové menu dovoluje SAVE CSV a číslo 000–999. Měřicí položky
zobrazují frekvenci, periodu, minimum, maximum, Δt, ΔV, střídu a TH/TL.
Vpp/DC/RMS jsou vpravo. Horní titulky A, GEN, časové základny a triggeru
zůstávají v běžných řádcích; dole je vybraná položka a stav 144D/144A.
K4 při měření vybírá položku. Kalibrace se otevře až dlouhým K2, nikoli
samotným vstupem do 144D/144A.

Změny rozsahu, sondy, Y, času, RUN/HOLD a generátoru se zachovají i po
opuštění interlace. Původní nastavení nezávislých kanálů se obnoví.

## Dostupné a blokované funkce

| Funkce | Stav |
| --- | --- |
| Vstup A; oba ADC na A; nominálně 144 MSa/s | Implementováno; instalované W1.2, kontrola značky i ROM CRC |
| 50/100/200/500 mV a 1/2/5/10 V/div | Všech osm fyzických rozsahů; nekalibrovaný rozsah nesnímá |
| DC/AC, sonda ×1/×10, inverze, Y | Implementováno; ×10 mění údaj na hrotu sondy |
| 0,1/0,2/0,5 µs/div | 30 pixelů/div, interpolace Q16 |
| RUN/HOLD, AUTO/NORM/SGL | Software trigger v zachyceném rámci |
| Vpp, DC, RMS, minimum, maximum | Z celého korigovaného rámce |
| Frekvence, perioda, střída, TH/TL | Alespoň dvě vzestupné hrany, dostatečná amplituda a žádný raw clip |
| T1/T2, Δt/ΔV | Nad korigovaným proudem s vlastní časovou stupnicí |
| CSV | Všech 1024 vzorků, čas v ps, napětí v µV, původ ADC, raw clip |
| Kalibrace a uložení | 0 V + známá kladná DC reference, každý rozsah; společný WPT |
| GEN | Běžné analogové průběhy a PWM; nastavení v menu GEN |
| Nezávislé B/C/D, A±B | Zablokováno, oba ADC používají A |
| FPGA oversampling, FFT/spektrum, XY, chart, detector, sériový dekodér | Zablokováno |
| Šířkové/časové triggery, holdoff, hardwarový SGL, autoset, persistence | Zablokováno |
| BUF/DAT/BMP/CFG, automatické ukládání | Zablokováno; dostupné jsou CSV a společná kalibrace |
| Sweep/burst, noise/sériový/arbitrary generátor | Nelze aktivovat během interlace |

Při vstupu z XY/chart, sweep, downconversion, kalibrace nebo nepodporovaného
generátoru se režim odmítne. Nativní zpracování oddělených kanálů se nad
prokládaným rámcem nespouští. Běžná akvizice je opět dostupná po změně vazby.

## Jedna standardní kalibrace A+B+144

**Při vybraném A nebo B podržet K2 asi 3 sekundy, potom uvolnit.** V režimu
144 je nezávislé B nedostupné, proto se vybírá A. V běžném měření se
kalibrace z A i B spouští stejně a kalibruje oba kanály současně.

Na vstupy A i B přivést stejnou referenci ze společného zdroje, se společnou
zemí. Není to kopírování koeficientů: každý ADC a vstup dostane vlastní
zero/gain i korekci polohy. Interní přesměrování B na A nenahrazuje připojení
reference na fyzický vstup B při kalibraci jeho vstupní cesty.

1. První obrazovka nabídne původní korekci hodin PPM. GEN je dočasně
   nastavený na nominální 1 MHz pro nezávislý čítač. Levý přepínač mění
   PPM; pokud se hodiny nekalibrují, **K2** ponechá dosavadní hodnotu.
2. Odpojit GEN/signál a **uzemnit A i B**. **K2** spustí automatickou
   kalibraci nuly a závislosti offsetu všech osmi rozsahů současně.
   Měří se dvě polohy offsetu a následně se kontroluje nulový bod v poloze
   používané pro napěťovou referenci. Na W1.2 se při stejném uzemnění
   automaticky změří i skutečné zapojení obou ADC na A v interlace.
3. Pro zobrazený rozsah připojit **stejné známé kladné DC napětí na A+B**.
   Zadat jeho skutečnou hodnotu v µV: levý přepínač ±0,1 % výchozí hodnoty,
   pravý ±1 %. Potvrdit **K2**. Program načte nový záznam A/B a potom
   automaticky záznam 144 ze vstupu A; zkontroluje ořezání, odezvu a limity
   korekcí. Při úspěchu přejde na následující rozsah.
4. Reference se nastavuje **jednou pro každý rozsah**, nikoli zvlášť pro A,
   B a 144. Zisk se vypočítá z nulového a referenčního měření; není potřeba
   samostatně ručně ladit gain každého kanálu.
5. Po osmi rozsazích: **K2 uloží kalibraci pro baterii**, **K3 pro napáječ**,
   společně A+B+144 do WPT/WPB. Do tohoto potvrzení se nic nezapisuje
   a aktivní původní koeficienty se nenahrazují. Po úspěchu se vrátí měření.
6. **K4 zruší průvodce bez nového zápisu.** Při chybě měření nebo uložení
   se zobrazí chyba; K2 vrátí neúspěšný krok, dalším K2 se měření nebo
   uložení zopakuje. Chyba uložení obnoví původní koeficienty v RAM.
   Již započatý potvrzený zápis může zanechat platnou novou generaci na
   disku, i když následná kontrola/close selže; viz níže.

| Rozsah | Výchozí kladná reference (3 div) |
| --- | --- |
| 50 mV/div | 150 mV |
| 100 mV/div | 300 mV |
| 200 mV/div | 600 mV |
| 500 mV/div | 1,5 V |
| 1 V/div | 3 V |
| 2 V/div | 6 V |
| 5 V/div | 15 V |
| 10 V/div | 30 V |

Lze zadat jinou známou referenci, pokud není ořezaná a rozdíl ADC je
alespoň osm kroků. Výchozí hodnota je nižší než dřívějších 5 div, aby
zůstal prostor ADC při pevně nastavených offsetech interlace. Reference
se připojuje přímo na vstupy, bez ×10 sond. Interní GEN není nezávislou
absolutní napěťovou referencí a nepokrývá všechny rozsahy.

Běžná část zachovává Ka1/Ka2/Ka3 a Kb1/Kb2/Kb3 i nízkou/vysokou bateriovou
sadu. Korekce nuly/zisku, vykreslení, trigger a časové vyrovnání analogové
hrany používají shodné pořadí operací. Nulová odezva se navíc ověří v poloze
pro referenci; saturovaná/nelineární odezva se odmítne. Dělení při bateriové
interpolaci je blokováno pro shodné nebo obrácené referenční úrovně baterie.

144 má absolutní zero/gain A a relativní gain/bias B měřené v jeho skutečném
zapojení, s pevnými hardwarovými offsety 40. Y je softwarový posun. Není
potřeba znovu ručně přepojovat všechny reference pro 144. Uložený formát
144 v1 obsahuje jednu aktuální sadu koeficientů, ne samostatnou bateriovou
interpolaci interlace. Volba baterie/napáječ nadále vybírá původní A/B sadu.
DC měření neprovádí časovou kalibraci ADC; pořadí B→A a nominální posun
zůstávají podle dřívějšího testu, automatický časový trim se nepřidává.

## Přeskakování údaje při blikání — opravené kroky

Původní Calibrat.c měnil rozsah/offset v bloku Blink po načtení vzorků,
používal neznaménkový mezivýsledek odečtení ADCoffset a sdílený textový
buffer mezi různými kroky. Nový postup vždy nejprve nastaví celou cestu,
počká 120 ms, resetuje FIFO, provede priming, zahodí 152 historických dvojic
a vyhodnotí 512 nových dvojic. Všechny rozdíly ADC se počítají se znaménkem.

Blink/Twink neřídí výpočet, změnu reference, koeficientů ani ADC offsetů.
Zobrazená reference se překreslí pouze při skutečné změně hodnoty/kroku.
Potvrzovací tlačítka musí být uvolněna, než lze potvrdit další krok;
fronta událostí při uvolnění nepřeskočí další referenci. PC test přepíná
Blink/Twink po celou dobu průvodce. Odstranění skutečného přeskakování na
přístroji ještě musí potvrdit uživatel.

## Společný soubor kalibrace

Kalibrace je v **`<sériové číslo>.WPT`** na interním disku:

| Bajty | Obsah |
| --- | --- |
| 0–511 | Původní formát Wildcatu v6: parametry, kalibrace A/B, baterie, PPM a původní kontrolní součet |
| 512–1023 | Interlace v1: osm rozsahů, sériové číslo, CRC instalačního W1.2, generace a vlastní CRC-32 |

Společný průvodce zapisuje po potvrzení aktualizovaný nativní prefix i
rozšíření 144 v jedné operaci. Běžné uložení parametrů přes Save_Param(0)
zachovává platné rozšíření 144. Číslované CFG kalibraci nenahrazují.
Chybějící WPT se vytvoří z aktuálních původních parametrů. Při tvorbě
nového nativního prefixu se vazba 144D/144A ukládá jako DC/AC, aby starší
Wildcat nedostal neznámý index do svého menu. Volba 144 se provádí v menu.

Před přepsáním existujícího WPT se jeho platná data zapíší a zpětně ověří
jako **`<sériové číslo>.WPB`**. Potom se zapíše a zpětně ověří nový WPT.
Při poškozeném hlavním souboru lze načíst záložní kopii. Obnova nepřepisuje
jedinou platnou WPB před opravou WPT. Chyba čtení blokuje zápis. Po chybě
zápisu/close může být dostupná stará nebo již úplná nová generace, vždy
s kontrolou platnosti. Nejde o záruku atomicity FAT při výpadku napájení.
WPT i WPB uchovávat společně.

Starý 512B WPT zůstává čitelný. Pokud společné rozšíření není platné,
program načte nejnovější platnou kalibraci ze starých IC144A.CAL/IC144B.CAL.
Samotné načtení nic nezapisuje; následující výslovné uložení ji převede do
WPT. Staré CAL se nemažou. Nová společná kalibrace při uložení nahradí jejich
převzaté koeficienty čerstvě změřenými hodnotami. Poškozená kalibrace, jiný přístroj nebo jiný
obraz FPGA se odmítnou.

Logický obsah je 1024 B; kvůli sektorovému API AlterBIOS může mít soubor
fyzicky 4096 B. Starší firmware rozšíření nezná a může ho při vlastním
uložení odstranit. Před návratem ke staršímu firmware zachovat WPT/WPB na PC.

## Čas, trigger a CSV

Záznam má jedno priming čtení, 152 zahazovaných dvojic a 512 platných dvojic.
Výstup je 1024 vzorků Q10 v pořadí B→A. Akvizice má timeout. Rámec trvá
nominálně 7,11 µs; mezi rámci jsou mezery, software trigger může událost
minout. Nulový časový výsledek znamená nesplněné podmínky odhadu.

PPM se přebírá z původního uloženého nastavení Wildcatu, platný rozsah je
±999 ppm. Kladná hodnota znamená vyšší skutečný takt: frekvence se zvětší,
intervaly zkrátí. Bez nezávislé reference je 144 MSa/s nominální hodnota;
PPM se neurčuje z interního GEN.

V běžné skupině souboru vybrat SAVE / číslo / CSV a **K3**. Soubory jsou
I144000.CSV až I144999.CSV. Existující soubor se nepřepisuje (status 200).
Exportuje se celý poslední platný rámec, včetně sondy, inverze a PPM.
Čas 0 je první platný B vzorek, nikoli trigger. Sloupce:
`sample,time_ps,A_uV,adc,raw_clip`. Řádky # jsou metadata, sektorové doplnění
jsou prázdné řádky. Data se ukládají přes AlterBIOS na disk; firmware,
SYS, FPGA obraz a původní kalibrační koeficienty v MCU flash se nezapisují.

## Sestavení a ověření na PC

```powershell
./WildcatV6.6/build.ps1 -ToolchainBin "$env:LOCALAPPDATA/Programs/gcc-arm-none-eabi-9-2019-q4-major/bin"
./tests/wildcat144/run.ps1
```

Výstupy: `bin/WildcatV6.6/app1.hex` a tři páry APP1A/B/C.ADR + .BIN.
FPGA musí být již instalované W1.2 z `bin/FPGAW1_2/`. Touto změnou se
QuadPawn, SYS ani FPGA nepřekládaly a do zařízení se nic nenahrávalo.
Uživatel u předchozí verze potvrdil start a běžné měření s instalovaným
W1.2 přes ADR/BIN; FPGA HEX hlásil ERR a FPGALOAD hlásil unsupported.
Diagnostika FPGALOAD pokračuje samostatně; její chyba zde není prohlášena
za vyřešenou.

| Provedená kontrola na PC | Výsledek |
| --- | --- |
| ARM GCC 9.2.1, -Wall -Werror, linker | PASS |
| Zpracování interlace | PASS, 262708 kontrol |
| Akvizice s mock sběrnicí/hodinami | PASS, 207 kontrol |
| GEN registry DC/PWM/DC | PASS, 14364 kontrol |
| Fáze/text | PASS, 788 kontrol |
| Scope144: osm rozsahů, čas, měření, trigger, menu, CSV | PASS, 1965 kontrol |
| Skutečný souborový modul s mock BIOS | PASS, 362 kontrol; WPT/WPB, převod, zachování A/B, 512/4096B sektory, chyby zápisu/čtení |
| Skutečný Wildcat144_Service s mock BIOS/registry | PASS, 129 kontrol: menu, DC/AC, rozsah, GEN, RUN/HOLD, CSV, návrat |
| Společné kalibrační jádro | PASS, 20944 kontrol: A/B/144, všech osm rozsahů, tlačítka, stabilní reference, chyby, korekce/trigger |
| Skutečný Calibrat() s mock BIOS a registry | PASS, 1324 kontrol: celý průvodce, baterie/napáječ, držení tlačítek, zrušení, rollback, blikání |
| Wildcat HEX/BIN/ADR a poškozené kopie | PASS, 9 scénářů |
| Kontrola zachovaných instalačních obrazů FPGA/QuadPawn | PASS, 186664 kontrol bajtů/záznamů; bez nahrávání |
| Nové menu, akvizice a fyzické uložení společného WPT na DS203 | **Neprovedeno** |

Logy: `build/wildcat144_tests/`. Historická měření diagnostiky jsou v
[DIAGNOSTICS.md](DIAGNOSTICS.md); nedokládají běh této nové integrace.
Výstupy: APP1A 52300 B, APP1B 49580 B, APP1C 59048 B.
HEX SHA-256: `aa7704da6c2b2a638c5eabfb38b467b3cda30257ff01ce1fe678541cb996592a`.
Text 159265 B, inicializovaná RAM 1848 B, BSS 44760 B, mezera před
počátečním SP 2544 B. Pracovní buffer 5424 B se znovu používá; nepřibyl
nový rámcový buffer. Pracovní stav a zálohy společné kalibrace používají
původní 16KiB DataBuf při zastavené akvizici. Velikost kontroluje překladač. GCC -fstack-usage: Wildcat144_Service 288 B,
Calibrat 88 B, File144_WptSave 960 B, File144_CalLoad 224 B, File144_NativeLoad 736 B.
Jde o jednotlivé funkce, nikoli měření maxima celého řetězce, přerušení
nebo funkcí SYS na zařízení. Skutečný běh a uložení se musí ověřit na HW.
