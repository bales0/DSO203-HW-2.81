# Historie diagnostiky 144 MSa/s

Toto je záznam předchozí diagnostické etapy. Časové údaje, velikosti a věty o tehdy neprovedených testech jsou historické. Aktuální implementace a výsledky jsou v [README.md](README.md).

# Wildcat V6.6 — vývojová verze pro DS203 HW 2.81/2.82

Samostatná kopie místního Wildcat V6.5b. Původní V6.5b, SYS a FPGA
zůstávají zachované. Aktuální změny se týkají pouze této aplikace:

- Označení V6.6 při spuštění a výstupy do `bin/WildcatV6.6/`.
- Při startu se explicitně čte rodina, protokol a značka FPGA. Každý
  selector se zapíše dvakrát podle latche sériového rozhraní W1.1.
  Po čtení se obnoví selector 0 a datový režim sběrnice.
- Na úvodní obrazovce se profil FPGA vypisuje na samostatném řádku y=10,
  mimo údaje SYS (y=70), HW (y=90) a AlterBIOS (y=110). Původní příznaky
  FPGAver/FPGAsubVer nyní vycházejí ze známého profilu a schopností.
  Neznámý profil nepovolí rozšířené funkce W1 ani FPGA oversampling.
- `Interlace144.c` zpracuje platné okno záznamu, vypočítá relativní
  kalibraci ADC ze dvou DC úrovní a vytvoří korigovaný proud B→A v Q10.
  Běžná měřicí smyčka toto rozhraní nepoužívá; volá jej nová samostatná
  experimentální obrazovka s akvizicí, kalibrací a testem fáze K3.
- Při startu i na experimentální obrazovce se zobrazuje `FPGA W1.1`
  nebo `FPGA W1.2`. Označení W1.1 nahrazuje dřívější W1.1-compatible;
  detekce a povolené funkce se tím nemění.

| Profil | Rodina, selector 0 | Protokol, selector 1 | Značka, selector 2 |
| --- | --- | --- | --- |
| W1-compatible, starší profil | 0x5731 | 0 | 0 |
| W1.1, neoznačený | 0x5731 | 0x0101 | 0 |
| W1.2 | 0x5731 | 0x0101 | 0x0102 |
| Ostatní kombinace | neznámý profil | | |

Starší profil W1 je kompatibilní konzervativní rozpoznání, nikoli
ověření autorského bitstreamu. Jeho samostatný obraz nebyl testován.
Neoznačené obrazy se stejnou odpovědí nelze navzájem rozlišit. Registry
neověřují hash celého obrazu; funkce se přiřazují ze známých zdrojů.
Pro W1.1/W1.2 se deklaruje FIFO, 32bitový trigger, FreeRun, FPGA
oversampling a datová cesta pro prokládání. W1 nedostává profil pro
oversampling ani 144 MSa/s. Neznámá implementační značka nic nepovoluje.

## Příprava 144 MSa/s

Běžné osciloskopické menu se zatím nemění. Samostatná experimentální
obrazovka provádí jednotlivou akvizici, kalibraci a porovnání fáze K3. Pořadí B→A pochází z testu
INT144 v3 na HW 2.82 s původním W1.1 podle postupu.

### Rozhraní zpracování v RAM

- `Interlace144_Calibrate`: ze dvou DC záznamů odvodí relativní zisk a
  offset B vůči A. Zisk Q10 musí být 512–2048 (0,5–2), rozdíl úrovní
  každého ADC alespoň 8 kroků a průměry v rozsahu 10–245. DC záznamy
  s krajními hodnotami ADC 0/255 se odmítnou. Neúspěšná kalibrace
  zneplatní předchozí koeficienty; při neplatném/aliasovaném cíli se nic
  nezapisuje. Nejde o absolutní kalibraci napětí ani časování.
- `Interlace144_Process`: vynechá 152 úvodních dvojic a vrátí
  `corrected B[i], A[i], corrected B[i+1], A[i+1]`. Výstup je podepsaný
  `int32_t` v Q10 ADC krocích. B se koriguje `B*gain_q10+bias_q10`, A
  zůstává referencí násobenou 1024. Zachovají se i hodnoty mimo rozsah;
  nejsou tiše oříznuty. Statistiky počítají surové ořezané vzorky A/B a
  počet korigovaných B mimo rozsah ADC.
- Před zpracováním musí souhlasit profil FPGA a `input_key` kalibrace.
  Klíč vytváří budoucí volající z nastavení rozsahu, offsetů, směrování,
  režimu ADC a taktu. Každá změna nastavení musí vyžádat novou kalibraci.
  Modul sám skutečné registry konfigurace nečte a nezaručuje jejich shodu.
- Podporovaný vstup: 16bitové dvojice A v dolním, B v horním bajtu,
  po samostatném úvodním čtení pro nastavení FIFO ukazatele. Vstup stále
  obsahuje 152 zahazovaných dvojic. Toto okno je určeno pro `Trig_Always`;
  pro jiné triggery se nesmí bez ověření použít stejný postup.
- Limit vstupu je 4096 dvojic, po zahození musí zbýt alespoň 16. Kapacita
  výstupu je počet vzorků, tedy `2*(pair_count-152)`. Při chybném vstupu,
  nedostatečné kapacitě nebo překryvu bufferů se výstup ani statistiky
  nemění. Nízká a vysoká DC úroveň mohou mít rozdílné počty platných dvojic.
- Modul nealokuje heap ani velký statický buffer. Buffery poskytne
  volající. Pro běžné okno 512 dvojic je výstup 1024 vzorků / 4096 B;
  maximální vstup by vyžadoval výstup 31 552 B. To nelze automaticky
  přidat k ostatním bufferům Wildcatu; integrace musí respektovat RAM.

Samotný modul Interlace144.c neprovádí akvizici ani konfiguraci hardware.
Capture144.c zajišťuje čtení přes oddělené rozhraní; Phase144.c
porovnává časový posun se samostatnou korekcí obou režimů. Wildcat144.c obsahuje
obsluhu DS203 a experimentální obrazovku. Žádná z těchto částí nezapisuje
flash. Běžné měření, časová základna, normální triggery, měřicí funkce a
export ještě nejsou s 144 MSa/s propojeny. Diagnostické koeficienty uživatele
se do firmware nevkládají.

### Potvrzené výsledky na zařízení

Uživatel následně potvrdil funkci QuadPawn 2026.10.14, načtení označeného
W1.2 aplikací FPGALOAD, testy ID/1MHz/72MHz a návrat na W1.1. Tento test
neověřoval 144 MSa/s označeného W1.2. Uživatel také potvrdil start a běžné
měření první V6.6; současně nahlásil překrytí AlterBIOS údajem FPGA.
Původní překrývající údaj FPGA byl odstraněn. Na následnou žádost uživatele
se označení vrací na vlastní řádek y=10; jeho zobrazení na zařízení čeká na test.

Dřívější INT144 v3 při GEN→A a 1/2/4 MHz na původním W1.1 podle postupu
potvrdil nominální půlperiodové prokládání a pořadí B→A, bez ořezaných
vzorků. Relativní korekce v RAM byly S=1078/−3514 a I=1052/−885.
Přesná tabulka zůstává v [INT144.md](../QuadPawn/Programs/INT144.md).
Tyto údaje nejsou testem nového modulu Wildcatu ani absolutní kalibrací.

## Sestavení a ověření 14. 10. 2026

Z kořene projektu:

```powershell
./WildcatV6.6/build.ps1 -ToolchainBin "$env:LOCALAPPDATA/Programs/gcc-arm-none-eabi-9-2019-q4-major/bin"
```

Skutečně použito ARM GCC 9.2.1. Překlad a link s `-Wall -Werror` prošly.
HEX a tři ADR/BIN páry vznikly. Adresy jsou zachovány z V6.5b:

| Pár | Adresa | Velikost BIN |
| --- | --- | --- |
| APP1A | 0x0800C000 | 55 720 B |
| APP1B | 0x0802C000 | 49 088 B |
| APP1C | 0x08047000 | 47 332 B |

Linker kontroluje ROM a statickou RAM. `.bss` nyní má 44 208 B,
inicializovaná data 1788 B. Sdílený pracovní buffer diagnostiky má 5424 B:
po výpočtu koeficientů se DC záznamy přepoužijí pro zachycený rámec a
1024 výstupních vzorků. V běžném DataBuf se nic nemění. Mezi statickými
daty a počátečním SP zbývá 3156 B; skutečné maximum zásobníku na zařízení
zatím měřeno nebylo.

Automatické testy z kořene projektu:

```powershell
./tests/wildcat144/run.ps1
```

Skutečně prošlo 262 708 kontrol (většina je vyčerpávající kontrola dvojic):
oba profily W1.1/W1.2, všech 65 536 surových slov pro každý profil,
pořadí B→A, správná korekce, ignorování historie, minimální/maximální
okno, odlišné délky DC záznamů, kapacita a překryv bufferů, chybné
ukazatele, změna profilu/nastavení, ořezání ADC a limity zisku.
Testy běží na PC proti skutečnému C modulu; periférie DS203 se nespouštějí.
Log je v `build/wildcat144_tests/results.log`.

Aktuální HEX: SHA-256
`438bf9a9c5671d1e09a67f8d764a79394688d9166aed7ec1e96f1465d33dd547`.
Aktuální sestavení s novým úvodním řádkem FPGA dosud na zařízení ověřeno není.
K3 v předchozím sestavení uživatel potvrdil; viz výsledky níže.
Žádné automatické nahrávání se neprovedlo.

Historický návod V6.5b zůstává u původní aplikace a není vydáván za nový
ověřený manuál V6.6.

## Experimentální akvizice a DC kalibrace ve Wildcatu V6.6

Uživatel spustil experimentální obrazovku na DS203 a původně nahlásil
No DC response. Po opravě DC dokončil kalibraci, ale viděl rovnou čáru.
Po opravě návratu na PWM potvrdil průběh, 1024 vzorků, Raw clips A+B=0
a Corrected B outside ADC=0. Poslední DC výsledky byly A 148/176,
B 145/173, Gain Q10 1059 a Bias Q10 −1984. Uživatel následně potvrdil funkci generátoru a dokončení testu fáze K3
na profilu označeném tehdy W1.1-compatible; výsledky jsou uvedeny níže.
Nejde o změnu QuadPawn, SYS ani FPGA.

Po samostatném souhlasu s nahráním nového app1.hex a testem na zařízení:

1. V normálně spuštěném Wildcatu stisknout současně K3+K4 a uvolnit obě
   tlačítka. Kombinace se používá za běhu, nikoli při zapínání přístroje.
   Vyžaduje běžný osciloskopický režim: bez XY, SCAN/chart, sweep,
   downconversion, probíhající kalibrace a sériového/noise generátoru.
   Nevhodný režim zobrazí vysvětlení a návrat přes K4.
2. Na experimentální obrazovce propojit Wave Out (GEN) se vstupem A.
   K1 spustí kalibraci. A se dočasně nastaví na DC / 1 V, B interně
   na vstup A, oba offsety 128. Zůstane nominální takt 72 MHz a aktivní
   INTERLACE, bez FPGA oversamplingu a FreeRun.
3. GEN postupně nastaví nominálně 0,6 V (DAC 878) a 1,8 V (DAC 2633),
   vždy vyčká 120 ms a zachytí DC rámec. Po úspěchu se vypíše Gain Q10
   a Bias Q10. Vždy po obou dokončených DC záznamech se zobrazí
   `DC A low/high` a `DC B low/high`, tedy celočíselné průměry 512
   platných vzorků při nízké/vysoké úrovni, i když kalibrace selže.
   Při slabé DC odezvě nebo ořezání se kalibrace odmítne.
   Koeficienty platí pouze do opuštění této obrazovky.
4. K2 po kalibraci dočasně nastaví interní obdélníkový GEN na nominálně
   1 MHz. Získá jeden rámec při nominálních 144 MSa/s: jedno priming
   čtení, 152 úvodních dvojic pro zahození a 512 platných dvojic.
   Zobrazí počet 1024 korigovaných vzorků Q10, surové ořezané vzorky A+B,
   korigované B mimo ADC rozsah a bodový náhled prvních 384 vzorků.
   Náhled nemá časové/napěťové kurzory a není běžnou měřicí obrazovkou.
   Jeho vodorovný rozsah je nominálně 384/144MHz, přibližně 2,67 µs.
5. Po DC kalibraci i po záznamu se GEN vrátí na 0 V. K2 může pořizovat
   další jednotlivé záznamy. Při chybě přenosu se kalibrace zneplatní.
6. K4 zruší probíhající čekání nebo ukončí obrazovku. Při normálním
   opuštění se obnoví SEPARATE a původní vstupní rozsahy, vazby, offsety,
   časová základna, trigger a generátor podle nastavení běžného Wildcatu.
   Diagnostické koeficienty se zahodí. Obnova generátoru jej může znovu
   spustit podle původního nastavení; nejde o trvalé vypnutí Wave Out.

Čekání na Start+Full a přenos celého rámce mají společný timeout
nominálně 1000 ms. Časování používá 32bitový DWT cycle counter včetně
přetečení; před použitím se kontroluje, že čítač běží. FIFO se čte
synchronním BIOS Read_FIFO, bez Pawn DMA smyčky. K4 je kontrolováno
mezi čteními a při ustálení DC. Nejde o záruku obnovy při HardFault,
poruše paměťové sběrnice nebo vypnutí zařízení.

Obsluha dočasně blokuje přerušení generátorů TIM2/TIM4, nikoli časovač
kláves TIM3. Před návratem obnoví generátor a akvizici a poté povolení
přerušení. Fyzickou obnovu konfigurace musí ověřit test na zařízení;
hostitelské testy ji neměří.

Navíc skutečně prošlo 204 kontrol Capture144.c s náhradní sběrnicí a
hodinami: priming a pořadí dat, sentinel za bufferem, oba profily,
přetečení hodin, timeout během čekání i přenosu, zrušení před zahájením
i během čtení, chyby configure/start/priming/čtení a neplatné argumenty.
Spouští je stejný tests/wildcat144/run.ps1; log je capture-results.log.
Žádná z PC kontrol neprokazuje skutečných 144 MSa/s nového Wildcatu,
relativní kalibraci fyzických ADC ani nezávislou přesnost interního GEN.

## Oprava přepnutí GEN na DC (14. 10. 2026)

Původní přímé nastavení DAC nepřepínalo digitální výstup PB6 zpět na
plovoucí vstup, jak to dělá SYS při ANALOG_ARR. Pouhé zastavení TIM4
neznamená uvolnění jeho výstupního pinu. To je nalezený nedostatek,
který mohl ovlivnit DC výstup; příčina konkrétního No DC response tím
na zařízení ještě potvrzena není.

Sdílená implementace `inc/Gen144.h` nyní vypne TIM4 CH1 a časovače
TIM4/TIM7, DMA generátoru a DAC. Potom přepne PB6 na plovoucí vstup,
PA4 do analogového režimu a zapne DAC1 bez výstupního bufferu. Mění
jen potřebné bity GPIO, nikoli celé porty. Normální generátor se nadále
obnovuje stávající obsluhou při opuštění experimentální obrazovky.

Předchozí test `test_gen.c` prošel: 11 265 kontrol DC implementace
nad STM32 registerovými strukturami. Ověřuje všechny kombinace předchozích
režimů PA4/PB6, uvolnění PWM, vypnutí timer/DMA enable bitů, zachování
ostatních GPIO a nastavení registrů DAC pro všech 4096 hodnot.
Výsledek je v `build/wildcat144_tests/gen-results.log`; test spouští
`tests/wildcat144/run.ps1` spolu s 262 708 kontrolami zpracování a
204 kontrolami akvizice. Všechny prošly. Není to měření fyzického
napětí na GEN, analogové odezvy ADC ani ověření obnovy na zařízení.

Při dalším schváleném testu GEN→A je potřeba opsat obě dvojice DC A/B
low/high, případně Gain/Bias nebo přesné chybové hlášení. Tyto hodnoty
rozliší malou odezvu obou ADC od problému pouze jednoho kanálu.

## Oprava DC → obdélník → DC (14. 10. 2026)

Dokončená kalibrace po předchozí opravě ukázala odezvu obou ADC:

| Údaj | Výsledek uživatele |
| --- | --- |
| DC A nízká/vysoká | 149/176 |
| DC B nízká/vysoká | 146/173 |
| Gain Q10 | 1047 |
| Bias Q10 | −257 |
| Následný průběh | Rovná čára |

Konečné upřesněné koeficienty jsou 1047 a −257. Průměry na displeji
jsou zaokrouhlené dolů na celé ADC kroky, koeficienty používají přesnější
průměry Q10; nelze je přesně odvozovat z uvedených celých hodnot.

DC režim nově vypínal TIM4 CH1 přes CCER, ale původní Square() jej
nepovolil. Použitý SYS nastavuje PSC/ARR/CCR a GPIO, nikoli CCER.
To je konkrétní chyba přepnutí nalezená po hlášení rovného průběhu.

Square() nyní používá sdílený Gen144_SetSquare: zastaví timer, vypne
DAC/DMA, uvolní PA4, připojí PB6 jako AF push-pull, nastaví PSC=0,
ARR=71, CCR1=36, PWM1 s preloadem a aktivní vysokou polaritu. Výslovně
povolí CC1E, vynuluje čítač, provede update a spustí upcounter.
Frekvence 1 MHz a střída 50 % jsou nominální při 72MHz hodinách TIM4.
Před opuštěním diagnostiky se obnoví původní CCER/CCMR1, aby běžný
Wildcat převzal svůj původní PWM režim a povolení kanálů.

Aktuální test generátoru prošel: **14 337 kontrol**, včetně celé sekvence
DC → PWM → DC pro všech 256 kombinací předchozích GPIO režimů PA4/PB6.
Ověřuje povolení a polaritu kanálu, PWM mode, dělič, periodu, střídu,
restart čítače, režimy GPIO, návrat na DC a zachování ostatních pinů.
Nadále prošlo 262 708 kontrol zpracování a 204 kontrol akvizice.
Sestavení GCC 9.2.1 a kontrola adres HEX/BIN prošly.

Registerové testy nenahrazují osciloskopické měření výstupu. Vykreslený
průběh po této opravě ani fyzická obnova původního PWM zatím na DS203
ověřeny nejsou. Další test vyžaduje nové nahrání po souhlasu uživatele,
K1 kalibraci, K2 záznam a K4 návrat s kontrolou běžného generátoru.

## K3 — porovnání fáze odděleného a prokládaného režimu

Tato nová diagnostika je součástí Wildcatu V6.6; QuadPawn, SYS a FPGA
se nemění. Používá již aktivní známý profil W1.1 nebo W1.2. Na zařízení
zatím nebyla spuštěna a následující výsledky PC nejsou vydávány za měření.

Po schváleném nahrání aktuálního app1.hex otevřít za běhu K3+K4,
uvolnit tlačítka, propojit GEN→A a v diagnostice stisknout K3:

1. Program automaticky pořídí dvě DC úrovně zvlášť v režimu S (SEPARATE)
   a I (INTERLACE). B je v obou případech směrovaný na A, DC / rozsah A
   1 V / offsety 128, nominální takt obou ADC 72 MHz. Po úspěchu zobrazí
   samostatné S/I Gain/Bias a obě dvojice DC A/B low/high. Koeficienty
   z K1 ani jiného režimu se nepřebírají.
2. Opsat DC údaje a pokračovat K1. K4 ukončuje diagnostiku.
3. Postupně při nominálních GEN 1, 2 a 4 MHz program pořídí tři rámce
   odděleného a tři rámce prokládaného záznamu. Každý rámec má priming
   čtení, 152 zahazovaných dvojic a 512 platných dvojic. Historie se
   nezapočítá do fázového výpočtu ani do rozsahu a ořezání.
4. Každá frekvence má vlastní výsledkovou obrazovku. Opsat S/I lag8/near,
   E-/0/+, span A/B, clips A/B a Delta lag8 I-S. K1 přejde na další
   frekvenci; K4 ukončí celou diagnostiku. Generátor během čtení výsledků
   zůstává na 0 V a ADC jsou v odděleném režimu.
5. Po 4 MHz se zobrazí souhrn, zda všechny tři frekvence ukázaly stejný
   směr půlperiodového posunu. K1 vrátí experimentální menu, K4 běžný
   Wildcat. Po dokončení zůstane dostupná nová I kalibrace pro K2;
   časové pořadí K2 zůstává B→A a fázový test ho automaticky nepřepíná.

Výpočet vychází z INT144 v3: hledá posun B vůči A v rozsahu −2 až +2
ADC period po osmině periody. A se lineárně interpoluje, B se koriguje
vlastní DC korekcí daného režimu. Minimalizuje se součet absolutních
chyb; akumulátory jsou 64bitové, aby nedošlo k přetečení. Pět krajních
dvojic se vynechá z fitu kvůli rozsahu interpolace.

| Údaj | Význam |
| --- | --- |
| S / I | Oddělené / prokládané hodiny ADC |
| lag8 | Posun B vůči A v osminách periody 72MHz ADC |
| near | Počet kandidátů do 1 % od nejmenší chyby |
| E-/0/+ | Průměrné absolutní chyby Q10 při lag8 −4, 0 a +4 |
| span A/B | Max−min surových ADC kroků přes tři rámce |
| clips A/B | Počet hodnot ≤2 nebo ≥253 přes tři rámce |
| Delta lag8 I-S | Rozdíl posunu mezi režimy; −4 odpovídá B dříve než A |

Fit je neplatný při span menším než 10, jakémkoli ořezaném vzorku,
minimu na hranici hledání nebo více než třech blízkých kandidátech.
Platné fity s Delta −3 až −5 se označí jako Near half-cycle B→A,
s +3 až +5 jako A→B. Ostatní výsledky jsou nejednoznačné. Celkový
kladný závěr vyžaduje stejný směr při všech třech frekvencích.
Osm kroků je nominálně 13,89 ns, půl periody 6,94 ns; to není přesnost
metody. Interní GEN sdílí hodinovou základnu s ADC, proto neměří
nezávisle absolutní frekvenci, analogovou šířku pásma ani kalibraci napětí.

Nové obrazovky celé textové řádky doplňují mezerami. Uživatelův dřívější
údaj „0ready“ byl 0 s nevymazaným koncem RAM calibration ready, nikoli
zvláštní stav FIFO. Oprava nemění čtení ani vyhodnocení počtu ořezání.

## Aktuální PC kontroly K3

| Kontrola | Skutečný výsledek |
| --- | --- |
| Zpracování a RAM kalibrace | PASS, 262 708 kontrol |
| Akvizice s mock sběrnicí/hodinami | PASS, 204 kontrol |
| GEN DC/PWM/DC včetně 1/2/4 MHz | PASS, 14 364 kontrol registrů |
| Fáze a textové řádky | PASS, 788 kontrol |
| GCC 9.2.1 build a linker | PASS |
| Shoda HEX/BIN/ADR včetně ARM metadat | PASS, všechny tři oblasti |
| Kontrola instalačních souborů a chybových kopií | PASS, 9 kontrol; PowerShell 7 i Windows PowerShell 5.1 |
| K3 na DS203 | Uživatel potvrdil v předchozím sestavení, 1/2/4 MHz |

Test fáze ověřil všech 33 posunů na obou FPGA profilech, tři opakování,
relativní korekci zisku/offsetu, minimální a maximální okno, chyby
kalibrace a argumentů, slabé/konstantní/ořezané signály, oba směry
půlperiodového posunu a nejednoznačnost. Zvlášť ověřil 64bitové sčítání
chyb nad rozsahem uint32. Test textu ověřil odstranění zbytku ready,
celou šířku řádku, zkrácení příliš dlouhého textu a hranice bufferu.
Logy jsou v build/wildcat144_tests/ a spouští je tests/wildcat144/run.ps1.

Fázové akumulátory přepoužívají prostor výstupního Q10 bufferu, takže
se nealokuje další velký statický buffer. Volné místo před počátečním
SP je 3156 B. Pomocný překlad -fstack-usage ukázal pro main 136 B,
Wildcat144_Run přibližně 328 B a Phase144_Add 56 B na jednotlivý rámec
funkce. Tyto hodnoty nejsou měřením maxima celého řetězce volání,
přerušení ani funkcí SYS na zařízení.

Ukončení K4 má přednost před ostatními tlačítky a funguje i při čekání
na jejich uvolnění. Běžné měření, triggery, časová základna, měřicí
funkce a export dosud nejsou přepnuté na plné 144MSa/s měření.

### Opravený export a automatická kontrola instalačních souborů K3

Závěrečná kontrola našla neshodu HEX a APP1A.BIN na adrese 0x08019294.
64bitové dělení přitáhlo z libgcc samostatnou 8B sekci .ARM.exidx.
Linker ji správně umístil do první ROM oblasti před .data; HEX ji
obsahoval, ale původní export APP1A.BIN vybíral pouze .isr_vector a .data.
V BIN tedy byla na místě ARM metadat výplň.

Po souhlasu uživatele export APP1A.BIN zahrnuje také .ARM.extab* a
.ARM.exidx*. Závisí nově i na Makefile, takže se po změně exportu obnoví
bez ručního smazání výstupu. Adresy a programový obsah HEX se nezměnily.

Build.ps1 nyní po každém sestavení povinně spouští verify-artifacts.ps1.
Ten kontroluje syntaxi Intel HEX, délky a součty záznamů, EOF, duplicitní
adresy, adresové přetečení, startovací adresu, původní adresy všech ADR,
kapacity tří oblastí, shodu každého obsaženého bajtu HEX/BIN i nulovou
výplň mezer a pokrytí všech dat HEX exportovanými oblastmi. Nesoulad
ukončí build chybou před oznámením hotového firmware. SYS, FPGA a slot 4
nejsou součástí exportovaných oblastí.

Kontrolu lze spustit samostatně z kořene:

```powershell
./WildcatV6.6/verify-artifacts.ps1
```

Parametr -FirmwareDirectory umožňuje ověřit kopii stejného balíčku.
Skript používá jen PowerShell/.NET, nepotřebuje Python ani připojený DS203.

Regresní test test_artifacts.ps1 skutečně prošel v PowerShell 7 i Windows
PowerShell 5.1. Obsahuje 9 kontrol: platný balíček a odmítnutí vynechaných
ARM metadat, poškozeného BIN, chybného HEX součtu, chybějícího EOF,
nesprávného ADR, HEX dat ve slotu 4, duplicitního HEX záznamu a přidaného
bajtu BIN. Mění pouze vlastní kopie v build/wildcat144_tests/artifact_fixture/,
nikoli skutečný firmware. Spouští jej i tests/wildcat144/run.ps1;
výsledky jsou v artifact-results.log.

Aktuální výstupy si odpovídají: APP1A 55 720 B, APP1B 49 088 B,
APP1C 47 332 B. HEX SHA-256:
`438bf9a9c5671d1e09a67f8d764a79394688d9166aed7ec1e96f1465d33dd547`.
APP1A.BIN SHA-256:
`75597ec650bbddd4c0876ea94cf5e54e1589cf323715090aae1a98ce94e45894`.
Sestavení a kontrola instalačních souborů prošly. Nový úvodní řádek FPGA
na DS203 dosud ověřen není; další nahrání je samostatný krok po souhlasu.

### Výsledky K3 potvrzené uživatelem a úprava označení FPGA

Na HW 2.82 uživatel dokončil K3 při GEN→A na profilu registrů
0x5731 / 0x0101 / 0. Tento profil se nyní v aplikaci jmenuje FPGA W1.1;
dřívější text W1.1-compatible označoval tentýž profil, nikoli omezený režim.
Neoznačené bitstreamy se stejnou odpovědí nelze odlišit podle přesného
obsahu. W1.2 se rozpoznává samostatnou značkou 0x0102 v selectoru 2.

DC korekce v RAM: S gain/bias 1048/+1452 (A 148/176, B 144/171),
I 1058/−1853 (A 149/177, B 145/173).

| GEN | S lag8/near | S E(−4/0/+4) | S span A/B | I lag8/near | I E(−4/0/+4) | I span A/B | Delta |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 MHz | 0/1 | 1389/487/1278 | 74/72 | −4/1 | 488/1414/2369 | 74/72 | −4 |
| 2 MHz | 0/1 | 2248/534/2197 | 74/72 | −4/1 | 582/2316/4301 | 75/72 | −4 |
| 4 MHz | 0/1 | 3801/502/3601 | 67/65 | −4/1 | 587/3945/7544 | 72/70 | −4 |

Ořezání A/B bylo 0/0 ve všech záznamech. Všechny frekvence ukázaly
pořadí B→A a relativní posun půl periody ADC při nominálních 144 MSa/s.
Uživatel také potvrdil funkci generátoru. Nejde o nezávislou absolutní
kalibraci frekvence či napětí ani o test označeného W1.2.

Nové sestavení pouze mění viditelný název W1.1 a přidává úvodní řádek.
Stejnou funkci FpgaProfile_Name používá start i diagnostika. Detekční
registry, profily funkcí a akvizice se nemění. Na skutečný DS203 se
během této úpravy nic nenahrávalo; viditelnost řádku je zatím ověřena
pouze kontrolou souřadnic a 14pixelové výšky písma ve zdrojích SYS.

Úvodní údaj FPGA je nyní vodorovně centrován podle délky názvu
na 400pixelovém displeji (8 pixelů na znak); W1.1 a W1.2 začínají na
x=164, y=10. Uživatel potvrdil správné zobrazení předchozí varianty
s textem vlevo; tato úprava polohy na zařízení zatím ověřena není.
Sestavení GCC 9.2.1 a kontrola všech oblastí HEX/BIN/ADR prošly.
