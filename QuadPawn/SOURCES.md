# QuadPawn source inventory

Downloaded and checked on 2026-10-18. During the source import, existing files were preserved byte for byte, including the local runtime, I2C and HX4K USB Analyzer fixes. Only missing upstream files were added. Commits and per-file SHA-256 hashes are recorded in [SOURCES.json](SOURCES.json).

| Component | Local sources | Upstream revision |
| --- | --- | --- |
| Runtime, compiler, libraries and 18 core Pawn applications | [Runtime](Runtime/), [Compiler](Compiler/), [Programs](Programs/) | [QuadPawn](https://github.com/PetteriAimonen/QuadPawn), `704ea78c61738574bb62407f503fc49a13ffb405` |
| USB Analyzer, VHDL decoder, filters and testbenches | [Programs/USBAnalyzer](Programs/USBAnalyzer/) | [dso-quad-usb-analyzer](https://github.com/PetteriAimonen/dso-quad-usb-analyzer), `38853395d2964baeddff8846d196621f3e9787ce`; local HX4K port preserved |
| Logic Capture FPGA and original native logic analyzer | [Programs/LogicCapture](Programs/LogicCapture/) | [dso-quad-logic](https://github.com/PetteriAimonen/dso-quad-logic/tree/fpga_support), branch `fpga_support`, `a31f571ccd31e3a04206b98b893b0628a1b2cec6` |
| Original AlterBIOS implementation, FatFs, BIOS patches and generator tools | [Runtime/alterbios/source](Runtime/alterbios/source/) | [AlterBIOS](https://github.com/PetteriAimonen/AlterBIOS), `759775fe9f028f1fe39bd122c11043d55385862e` |

The Logic Capture import includes all 18 FPGA files: RLE encoding, FIFO, pipelined counters/subtraction, channel alignment, pin/timing constraints and six testbench sources. Its original C/C++ application and bundled source libraries are retained as reference. It is not an additional Pawn application and is not added to the active build. After this import, the authorized [Logic Capture HX4K port](Programs/LogicCapture/README.md) was implemented locally, with new `logiccap.amx` and `LOGIC.FPG` outputs in the root `bin` directory. The source manifest records these later changes separately from the import verification below.

The AlterBIOS snapshot is reference source. The active, locally repaired SYS/AlterBIOS 0.7 remains in [SYS_V1.64_AlterBIOS](../SYS_V1.64_AlterBIOS/). Neither that implementation nor the runtime's existing AlterBIOS API files were replaced.

Upstream releases and precompiled executables/firmware were excluded from this source import. During the import, existing firmware and AMX files in `bin` were not changed. The original author applications remain the same 19-program set. The alternate QuadPawn branches were checked; they add no other applications or FPGA HDL and their older runtime variants were not mixed into the active project.

Import-time verification covered 554 upstream source/support files and all 906 pre-existing project files. Added: 108 missing upstream files. Modified or removed pre-existing files: **zero**. Existing upstream files with local changes or different line endings were retained.

Known unavailable sources:

- The catalog's external [Resistance meter](http://www.mbehrens.nl/wordpress/?p=1) still returns HTTP 404 over both HTTP and HTTPS; no replacement source was found.
- HDL corresponding specifically to the old `TESTFPGA.BIN` example was not found in the checked author repositories/branches. The FPGA sources for Logic Capture and USB Analyzer are available locally.
