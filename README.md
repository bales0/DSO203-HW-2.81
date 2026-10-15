# DS203 firmware pro HW 2.81/2.82

Aktuální vývoj: [Wildcat V6.6 s 144 MSa/s](WildcatV6.6/README.md),
[instalační FPGA W1.2](bin/FPGAW1_2/README.md) a navazující
[QuadPawn 2026.10.14.2](bin/QuadPawnW12/README.md). PC kontroly prošly;
nové běžné položky 144D/144A a společná kalibrace WPT čekají na test
na zařízení. Společná kalibrace A+B+144: při vybraném A nebo B dlouze K2.

Původní zdrojové projekty jsou zachované vedle sebe:

- [Wildcat V6.5b](WildcatV6.5b/README.md) – osciloskopická aplikace.
- [SYS 1.64 + AlterBIOS](SYS_V1.64_AlterBIOS/gcc/README.md) – systémový firmware.
- [QuadPawn](QuadPawn/) – samostatný Pawn runtime a programy.
- [FPGA W1.1](W1.1%20FPGA/README.TXT) – zdroje FPGA pro Wildcat a HW 2.81/2.82.

Finální obrazy jsou v [`bin/`](bin/README.md), referenční obrazy v
[`reference_builds/`](reference_builds/README.md). Postupy sestavení jsou v
[BUILDING.md](BUILDING.md) a původ zdrojů v
[FIRMWARE_SOURCES.md](FIRMWARE_SOURCES.md).

Aktuální [manuál Wildcat V6.5b](WildcatV6.5b/Manual-V6.5b.html) je dostupný
také jako [PDF](WildcatV6.5b/Manual-V6.5b.pdf).
