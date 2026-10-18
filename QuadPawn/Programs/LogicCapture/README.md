Logic analyzer for DSO Quad
========================

![Header image](http://kapsi.fi/~jpa/stuff/pix/logic_header.jpg)

A simple logic analyzer application for the DSO Quad pocket oscilloscope.

See [this blog post](http://essentialscrap.com/dsoquad/logic.html) for more information and also [the wiki](https://github.com/PetteriAimonen/dso-quad-logic/wiki)

## Local port for DS203 HW 2.81/2.82

The Pawn application is [logiccap.pawn](../logiccap.pawn); the original native C/C++ application in this directory remains a reference. Upstream sources and notices are retained.

Copy **both** [logiccap.amx](../../../bin/QuadPawn/Programs/logiccap.amx) and [LOGIC.FPG](../../../bin/QuadPawn/Programs/LOGIC.FPG) from the root `bin` directory to the DSO drive. Use the current QuadPawn runtime and SYS/AlterBIOS. The old `Programs/LOGIC.FPG` is an iCE65 reference and cannot be used on HX4K hardware.

The application captures A/B/C/D at 72 MS/s, independently of Wildcat's 144 MS/s mode. A/B use the original DC/200 mV ADC threshold configuration; C/D use the board's digital inputs. Channel alignment and RLE compression are retained. FPGA storage has 4,096 words (4,095 usable); continuous capture throughput depends on transition rate and disk speed. Reaching full capacity stops the capture and marks the VCD incomplete, including saturation that occurred while the CPU was draining the FIFO.

The HX4K/CB132 design uses the HW 2.81 pin map, with C=K3 and D=M3. Register selection now uses the original board controls: **PC4/ClrR (D14)** selects identity `0x4C43`; **PC5/H_L (E12)** selects FIFO count when PC4 is low. Both low select FIFO data. Identity takes priority when both are high and never pops FIFO. Count bits 11:0 are occupancy; bit 15 remembers full capacity until reset. The application does not depend on parallel writes or WR/M9; the original parallel configuration block remains as reference source. LCD bus traffic cannot alter GPIO selection. A lost long-idle RLE marker in the original design is fixed.

Startup holds reset low/high for 1 ms, waits after selecting identity, and requires two matching reads with at most three attempts. A failure reports both actual IDs. The previous parallel-write build returned `0x1 / 0x1` on a physical device; that is not proof of an incompatible image, and prompted removal of the unverified write path. Install the updated AMX and FPGA image together. The GPIO selection revision still awaits device verification.

VCD output uses standard `1ns` units with exact integer conversion from 72 MHz ticks and retained fractional phase. Each transition is written in one file operation. Filename selection, file creation, every write and close are checked; disk failures cannot report a successful capture. New files use exclusive creation.

The FPGA configuration is temporary; QuadPawn restores installed W1.2 on application exit. No system FPGA image is written to MCU flash.

Build from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File QuadPawn/Programs/LogicCapture/fpga/build-hx4k.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File QuadPawn/build-w12.ps1
```

`tools/build-all.ps1` includes this FPGA build. Outputs go to `bin/QuadPawn/Programs`; intermediate files and logs go to `build/logic_hx4k`. The build requires GHDL in `tools/ghdl` and OSS CAD Suite.

Validation covers seven VHDL testbenches, including all four channel polarities, ADC-to-FSMC transfer, identity, FIFO reads/full/reset and long idle markers; Pawn VM tests check timestamps, image verification, cancellation and disk errors. The routed design passes the 72 MHz constraint. Loader tests use the actual HX4K image and verify restoration of W1.2. Physical capture and electrical bus timing still require device testing.
