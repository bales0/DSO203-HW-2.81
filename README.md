# DS203 Firmware - HW 2.81/2.82

Active development; latest component versions only.

| Component | Status |
| --- | --- |
| [SYS 1.64 + AlterBIOS 0.7](SYS_V1.64_AlterBIOS/) | Filesystem fixes in AlterBIOS/FatFs; original SYS code unchanged. |
| [FPGA W1.2](FPGAW1_2/) | Supports 144 MS/s; simulation and timing checks pass. |
| [QuadPawn 2026.10.17.3](QuadPawn/) | Snapshot/file I/O fixes; USB Analyzer and Logic Capture FPGA ports for HW 2.81; [program catalog](QuadPawn/Programs/README.md). |
| [Wildcat 6.6](WildcatV6_6/) | Normal and 144 MS/s modes, shared UI/calibration, filesystem save/delete fixes. |

**144 MS/s:** both ADCs sample channel A; analog B is unavailable. Select via the right rocker scan-mode cycle (purple overview). Calibration has passed on hardware; latest UI changes still need device testing.

Firmware and AMX programs: [bin/](bin/). Build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-all.ps1
```
