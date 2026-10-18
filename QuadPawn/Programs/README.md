# QuadPawn programs

Latest downloadable programs from the [official catalog](https://github.com/PetteriAimonen/QuadPawn/wiki), checked on 2026-10-17: all 18 core programs and USB Analyzer. Builds produce **19 AMX files** in [bin/QuadPawn/Programs](../../bin/QuadPawn/Programs/).

| Program | Purpose / requirement |
| --- | --- |
| advvolt | Advanced voltmeter |
| beeper | Continuity tester |
| calibrat | Pawn calibration (`calibrat.ini`, separate from Wildcat calibration) |
| charmap | Character/font viewer |
| fpga_test | Legacy FPGA test; bundled TESTFPGA.BIN targets older iCE65 hardware |
| freqresp | Frequency response measurement |
| getlicense | Serial number / license information |
| hello | Example program |
| i2c | I2C analyzer; K2 snapshot uses the corrected runtime BMP writer |
| logiccap | [Logic Capture](LogicCapture/README.md); HX4K LOGIC.FPG, four channels at 72 MS/s, VCD output |
| servo | Servo tester |
| signlgen | Signal generator |
| spec_an | Spectrum analyzer |
| specgram | Spectrogram |
| spectrum | Basic spectrum analyzer |
| torch | White-screen light |
| unittest | Library self-tests |
| voltmetr | Voltmeter |
| usb | [USB Analyzer](USBAnalyzer/README.md); HX4K USB.FPG for HW 2.81/2.82, hardware verification pending |

**HW 2.81/2.82:** copy each matching pair from `bin/QuadPawn/Programs/` to the DSO drive: `usb.amx` + `USB.FPG`, or `logiccap.amx` + `LOGIC.FPG`. Both FPGA designs target HX4K/CB132 and pass simulation and 72 MHz timing checks; device testing is pending. The original source-tree `LOGIC.FPG` and `TESTFPGA.BIN` still target older iCE65 hardware and are deliberately rejected by the loader.

Core sources come from [QuadPawn](https://github.com/PetteriAimonen/QuadPawn) commit `704ea78c61738574bb62407f503fc49a13ffb405`. The local I2C fix keeps K2 screenshot and K4 quit available while waiting for a trigger and identifies waiting/saving/error states. Its rotating indicator means trigger wait, not filesystem activity. Logic Capture adds the HX4K FPGA port, checked VCD writes, exact 72 MHz-to-nanosecond timestamps and FIFO saturation detection. Other core application sources match upstream. Full USB Analyzer source is under `USBAnalyzer/`, from [its own repository](https://github.com/PetteriAimonen/dso-quad-usb-analyzer) commit `38853395d2964baeddff8846d196621f3e9787ce`. The local USB port adds CB132 pin constraints, image identity verification, failed-initialization exit and clearing of prefetched bytes on reset. Upstream source/license notices are preserved. `USBAnalyzer/USB.FPG` is the author's original iCE65 release 0.1 retained as a reference; the usable HX4K build is only in `bin/QuadPawn/Programs/USB.FPG`.

Only original author applications are included. Local FPGALOAD, FPGAW12 and INT144 diagnostics are excluded as requested. USB Analyzer is an external application and was never included in the core 18-program set.

The official catalog also links a **Resistance meter**, but its original [download page](http://www.mbehrens.nl/wordpress/?p=1) currently returns HTTP 404; no downloadable release was recovered. This is the outstanding unavailable catalog entry.

[SOURCES.json](SOURCES.json) records source commits, URLs and SHA-256 hashes. Programs are rebuilt with this project's compiler; upstream precompiled AMX files were downloaded for comparison, rather than replacing the local builds.

QuadPawn runtime **2026.10.17.3** writes BMP images in 242 file writes instead of 48,017, closes on write errors, and processes the K3+K4 screenshot request outside the timer interrupt. File I/O fixes cover packed reads, failed opens, invalid handles, write/truncate errors, free space in bytes and the missing `f_unlink` native. Automatic filename selection fails safely when all 1,000 names are occupied or the disk reports an error. Host tests cover these paths; physical SAVE verification remains pending.
