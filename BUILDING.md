# Building DS203 firmware

Current development for installed W1.2 and interlaced acquisition is documented
in [Wildcat V6.6](WildcatV6.6/README.md). Build with ARM GCC 9.2.1 using
`./WildcatV6.6/build.ps1 -ToolchainBin <GCC9.2.1-bin>` and
`./QuadPawn/build-w12.ps1`. Export the installed FPGA image with
`python -B ./FPGAW1.2-test/export-system.py`.
Outputs are in `bin/WildcatV6.6`, `bin/QuadPawnW12` and `bin/FPGAW1_2`.
These commands never deploy to the device. The older V6.5b instructions follow.


Wildcat V6.5b sources are in `WildcatV6.5b/`. This is the STM32 Cortex-M3
application for DS203 hardware **2.81 and later**.
The FPGA Verilog sources in `W1.1 FPGA/` are a separate project; the application
build does not rebuild the FPGA.

## Windows

Requirements: GNU Make and an `arm-none-eabi` GCC toolchain including newlib.
The build was verified with **GNU Arm Embedded 7-2017-q4 (GCC 7.2.1)** and
MinGW GNU Make. Other compiler versions require separate verification.

From PowerShell in the project root:

```powershell
.\WildcatV6.5b\build.ps1
.\WildcatV6.5b\build.ps1 -Clean
```

The script looks for the toolchain on PATH, then in the Arduino installation
under `%LOCALAPPDATA%\Arduino15`. It looks for Make on PATH, then in the usual
MinGW and MSYS2 locations. It does not install tools or permanently change PATH.
To choose tools explicitly:

```powershell
.\WildcatV6.5b\build.ps1 -ToolchainBin 'C:\tools\gcc-arm\bin' -MakeExe 'C:\MinGW\bin\mingw32-make.exe'
```

`-Clean` removes generated outputs before rebuilding; `-Jobs 4` selects the
number of parallel compilation jobs. If local script execution is disabled,
run `powershell -ExecutionPolicy Bypass -File .\WildcatV6.5b\build.ps1` for this invocation.

## Command line / Linux

With `arm-none-eabi-gcc`, `arm-none-eabi-objcopy`, `arm-none-eabi-size` and GNU
Make on PATH, run from the project root:

```sh
make -C WildcatV6.5b -j4
make -C WildcatV6.5b clean
```

On MinGW use `mingw32-make` instead of `make`. A different toolchain prefix
can be selected with `make CROSS=/path/to/arm-none-eabi-` (avoid spaces).

## Outputs and checks

- `bin/Wildcat/app1.hex`: newly compiled firmware, suitable for the documented HEX update procedure.
- `bin/Wildcat/APP1A.ADR` + `bin/Wildcat/APP1A.BIN`: flash region beginning at `0x0800C000`.
- `bin/Wildcat/APP1B.ADR` + `bin/Wildcat/APP1B.BIN`: flash region beginning at `0x0802C000`.
- `bin/Wildcat/APP1C.ADR` + `bin/Wildcat/APP1C.BIN`: flash region beginning at `0x08047000`.
- `WildcatV6.5b/build/app1.elf`: executable with debugging symbols.
- `WildcatV6.5b/build/app1.map`: linker map for inspecting memory placement.

Header dependencies are tracked automatically, and changes to either linker
script trigger relinking. Compilation keeps `-O2`, `-fno-common`, and warnings
as errors. Assembly uses explicit Cortex-M3/Thumb options.

The linker retains the original BIOS address and the three flash regions:
`0x0800C000`, `0x0802C000`, and `0x08047000`. It checks flash space for initialized
RAM data and checks that static RAM data does not reach the initial stack pointer.
These checks do not measure runtime stack usage.

The complete HEX file spans three non-contiguous flash regions. The three
ADR/BIN pairs contain only their respective regions. Never concatenate them
or convert the complete HEX into one flat BIN: the gaps contain other firmware.
The build does not flash the device.

The matching SYS 1.64 with integrated AlterBIOS is a separate build in
`SYS_V1.64_AlterBIOS/gcc/`. Use Arm GCC 9.2.1 for SYS. On this Windows
machine it is installed at `%LOCALAPPDATA%\Programs\gcc-arm-none-eabi-9-2019-q4-major\bin`:

```powershell
$gcc9 = Join-Path $env:LOCALAPPDATA 'Programs\gcc-arm-none-eabi-9-2019-q4-major\bin'
$gccPath = $gcc9.Replace('\', '/') + '/'
& 'C:\MinGW\bin\mingw32-make.exe' -C 'SYS_V1.64_AlterBIOS\gcc' -j4 all "GCCPATH=$gccPath"
```

The final SYS ADR/BIN pair and HEX are in `bin/SYS164AB/`; ELF and map stay
in `SYS_V1.64_AlterBIOS/gcc/build/`. The BIN is byte-identical to the author's
26,303-byte release. QuadPawn is an independent runtime: its final firmware
is in `bin/QuadPawn/`, its compiled Pawn programs in `bin/QuadPawn/Programs/`,
and its intermediate files and C library in the component `build/` directories.
The supplied FPGA W1.1 ADR/BIN pair is in `bin/FPGAW1_1/`; FPGA synthesis is separate.
The original images and their addresses remain in
[`reference_builds/`](reference_builds/README.md).

A successful build verifies compilation and linking. Operation on physical
hardware still needs testing.
