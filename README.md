# DS203 Firmware - HW 2.81/2.82

Active development; latest component versions only.

| Component | Status |
| --- | --- |
| [SYS 1.64 + AlterBIOS](SYS_V1.64_AlterBIOS/) | Preserved system firmware. |
| [FPGA W1.2](FPGAW1_2/) | Supports 144 MS/s; simulation and timing checks pass. |
| [QuadPawn 2026.10.17.1](QuadPawn/) | Updated FPGA loader and file selector. |
| [Wildcat 6.6](WildcatV6_6/) | Normal and 144 MS/s modes, shared UI and calibration. |

**144 MS/s:** both ADCs sample channel A; analog B is unavailable. Select via the right rocker scan-mode cycle (purple overview). Calibration has passed on hardware; latest UI changes still need device testing.

Firmware and AMX programs: [bin/](bin/). Build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-all.ps1
```
