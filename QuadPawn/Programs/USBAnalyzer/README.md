# USB Analyzer for DS203 HW 2.81/2.82

Port of Petteri Aimonen's USB Full Speed (12 Mbit/s) analyzer to iCE40HX4K-CB132. Original license: GPL v3; see [README.txt](README.txt).

Copy **both** [usb.amx](../../../bin/QuadPawn/Programs/usb.amx) and [USB.FPG](../../../bin/QuadPawn/Programs/USB.FPG) to the root of the DSO drive. Use the current QuadPawn runtime and SYS/AlterBIOS. The `USB.FPG` beside this README is the original iCE65 reference, not the file to install.

The application temporarily replaces the oscilloscope FPGA configuration. QuadPawn restores the installed W1.2 on exit, including initialization failure. The system FPGA image in MCU flash is not written.

Connect USB D- to channel A, D+ to channel B, and probe ground to USB ground. The decoder uses both analog channels in DC/200 mV range at 72 MHz. It is independent of the 144 MS/s oscilloscope mode. Original filters and the 8,192-entry FPGA FIFO are retained.

The port uses the board's parallel bus, including WR on M9, and checks decoder identity `0x5542` after loading. Reset also clears the application's prefetched data. Failure to initialize exits the application instead of entering the capture loop.

Build from the repository root (GHDL 6.0.0 in `tools/ghdl`, OSS CAD Suite):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File QuadPawn/Programs/USBAnalyzer/fpga/build-hx4k.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File QuadPawn/build-w12.ps1
```

The root `tools/build-all.ps1` also builds this FPGA image. Outputs go to `bin/QuadPawn/Programs`; logs and intermediate netlists go to `build/usb_hx4k`.

Validation: eight original VHDL testbenches, an ADC-to-MCU integration test (identity, packet payload/EOP, FIFO count/pop, reset and bus release), a Pawn VM decoder test, and loader/restore tests using the actual HX4K image. Routing passes the 72 MHz clock constraint (111 MHz reported maximum). Electrical bus timing and capture on a physical DSO remain to be verified.
