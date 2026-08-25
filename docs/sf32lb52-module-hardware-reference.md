# SF32LB52-MOD-1 Hardware Reference

Last reviewed: 2026-08-04

This is the source-backed module-level hardware baseline for Huangshan Pi. It
does not replace the board BSP, the Runtime capability contract, or physical
board probes. A capability described by the module datasheet is not evidence
that the Huangshan Pi carrier board wires it, fits the corresponding component,
or safely exposes it to an App.

## Source And Evidence Levels

Primary source reviewed in full:

- `DS5203-SF32LB52-MOD-1 Technical Specification`, version 0.2, 2025-03.
- Analysed source: `/Users/hushaohong/Desktop/huangshanpi-analysedoc/`
  `2fb11b76-b536-46b1-99d1-ef31cddbd021_origin.pdf`.
- SHA-256: `419d26d7f9fe354e720eaa086d4fa54ab797b6112f9eb4722b57e6ab89af19b4`.

Evidence labels used below:

- **Verified board**: reproduced on the current Huangshan Pi and documented in
  this repository.
- **Module specification**: capability or electrical rule of SF32LB52-MOD-1;
  carrier-board wiring is not implied.
- **Reference design**: a vendor development-board allocation, not the
  Huangshan Pi schematic.

## Current Board Facts

- **Verified board**: target is `sf32lb52-lchspi-ulp`, fitted with
  `SF32LB52-MOD-1-N16R8`. The module specification identifies its MCU as
  SF32LB525UC6 and its integrated memories as 16 MB QSPI NOR plus 8 MB OPI
  PSRAM. The module envelope is 27.9x18.0x3.1 mm.
- **Verified board**: CO5300 is a 390x450 QADSPI AMOLED and FT6146 is the
  touch controller. The app-layer display contract is RGB565, 16 bpp.
- **Verified board**: the removable Micro SD socket uses SPI1, not SDIO:
  PA24/PA25 data, PA28 clock, PA29 chip select, PA27 card detect.
- **Verified board**: LSM6DSL responds on I2C3 address `0x6a`. Do not infer
  LTR303 (`0x29`) or MMC56X3 (`0x30`) presence from older example output; the
  current Runtime probe reports both absent and must retain that fallback.
- **Verified board**: KEY1 is PA34 and KEY2 is PA43; PA34 is also a reset/ADC
  sensitive pin and PA43 is interrupt-capable. Apps may use only the Runtime's
  read-only key API unless the BSP contract changes.
- **Verified board**: VBAT/charger reporting is read-only; AW32001 control is
  intentionally not an App capability. The RGB LED, microphone and audio
  output have separate Runtime ownership.

## Processor, Memory, Graphics, And Audio

### Module Specification

- HCPU: Arm Cortex-M33 STAR-MC1, up to 240 MHz, 512 KiB retention SRAM,
  32 KiB I-cache plus 16 KiB D-cache, FPU and MPU.
- LCPU: Arm Cortex-M33 STAR-MC1, up to 24 MHz and 64 KiB retention SRAM.
  The project's commonly cited 576 KiB on-chip SRAM is the sum of those two
  cores; it must not be treated as a single freely interchangeable heap.
- ePicasso 2.0 supplies 2D/2.5D rotation, scaling and mirroring. Its maximum
  resolution is 512x512, so the 390x450 panel is inside the engine limit.
- eZip 2.0 supports hardware lossless image/animation decompression and can
  feed ePicasso without an intermediate buffer. This is module support, not a
  promise that a chosen Runtime asset format is enabled.
- LCDC supports serial SPI, dual-SPI, quad-SPI and parallel 8080. Serial mode
  supports 3-wire/4-wire and dual/quad data lines; RGB332, RGB565 and RGB888
  are supported at the controller level. The CO5300 path remains BSP-owned.
- The source is internally inconsistent about audio channel count: its feature
  table says one 24-bit DAC and one 24-bit sigma-delta ADC, while its detailed
  peripheral text says two of each. Both sections specify 8-48 kHz operation;
  the ADC supports single-ended or differential microphones and MIC_BIAS is
  1.4-2.8 V, up to 2 mA. Do not use this datasheet alone to choose a channel
  count or prove a board mic, amplifier or speaker route.
- Audio pins are `AU_DAC1P_OUT`, `AU_DAC1N_OUT`, `MIC_BIAS`, and
  `MIC_ADC_IN`. The current board's AW8155/SPK route is documented separately;
  raw codec/I2S control remains outside the App API.

## Electrical And Pin Constraints

### Module Specification

- VSYS is 3.2-4.7 V for direct Li-ion supply. With an external DCDC/LDO supply,
  it is 3.7-4.7 V and the vendor recommends 3.8 V. Absolute maximum VSYS is
  4.7 V. Normal operating temperature is -40 to 85 C.
- All module I/O is 3.3 V. Do not attach 1.8 V or 5 V logic without a verified
  level-shifting design. At 3.3 V, input-high minimum is 0.7*VDD and
  input-low maximum is 0.3*VDD.
- `VBATS` is the dedicated battery-voltage sensing input (0-4.7 V). It must
  receive the measured voltage directly: the vendor checklist explicitly says
  not to use an external resistor divider. It has a separate ADC path from the
  GPIO analog channels.
- `VDD33_VOUT2` is a 3.3 V LDO output, not an arbitrary board power rail. Its
  maximum external load is 150 mA and its attached capacitance total must not
  exceed 7.4 uF. Treat both limits as PCB-design constraints, not an App power
  budget.
- GPADC is 12-bit, up to 4 MS/s. It supports seven external single-ended
  channels at 0-3.3 V plus the battery measurement, or three differential
  pairs at -2.1 to +2.1 V. GPIO channels can use one-shot or continuous modes,
  timer/software triggering and DMA. Only pins marked `GPADC_CHx` are analog
  inputs; other GPIO must not be assumed analog-capable.
- Every `PAxx_TIM` mux option can provide PWM, but that is not permission to
  remux a pin already assigned to display, storage, power, key, touch or debug
  hardware.
- PA22/PA23 are unavailable externally when the selected module variant fits
  its optional 32.768 kHz crystal. The current board's fitted-crystal state has
  not been independently verified, so reserve rather than allocate them.
- The module exposes 68 pins. PA18 and PA19 also have SWD muxes; the vendor
  reference design uses their debug-UART mux through 100-ohm series resistors.
  Do not repurpose either without the actual Huangshan Pi schematic and a
  recovery path.
- PA35/PA36 are USB FS D+/D- mux options. Module support for USB host/device
  does not confirm the board's connector, routing, protection, or power switch.

### Storage And Reset Reservations

- The vendor states that if a module variant has internal NOR/NAND flash,
  MPI2 pins PA12-PA17 cannot be used externally. This applies to the project's
  N16R8 interpretation: reserve PA12, PA13, PA14, PA15, PA16 and PA17.
- SDMMC1/SDIO and MPI2 share I/O and cannot operate together. The board avoids
  that conflict by using the Micro SD socket over SPI1; do not redesign it as
  an SDIO device while its fitted module flash needs MPI2.
- The source is internally inconsistent about the flash interface name: the
  feature list says `MPI`, the functional block diagram labels the integrated
  QSPI NOR connection `MPI3`, and the detailed schematic and exposed PA12-PA17
  muxes label it `MPI2`. The detailed schematic visibly connects its NOR to
  PA12-PA17. The verified board reservation and BSP configuration take
  precedence over these naming inconsistencies.
- PA34 has a hardware long-press reset function: high for 10 seconds. The
  vendor reference calls for a 10-kohm pull-down. This explains why PA34 must
  never be driven or continuously repurposed by a product App; it is in
  addition to its board KEY1 and GPADC roles.

### Pin-Numbering Caveat

The source's module pin table labels PA34 as `GPADC_CH7`, while its generic
development-board allocation table labels PA34 as `GPADC_CH6`. The Huangshan
Pi official ADC example and this repository's physical results use the board
driver's channel 6 for PA34. Board BSP mapping wins over either generic table;
do not derive ADC device-channel numbers solely from the module datasheet.

## Communications And RF

### Module Specification

- Bluetooth is dual-mode 5.3 with BLE Audio support. It is a 2.402-2.480 GHz
  radio, not Wi-Fi hardware. The default Runtime remains BLE GATT transport.
- BLE transmit power is configurable from -20 to +19 dBm; typical receiver
  sensitivity is -100 dBm at BLE 1 Mbps and -97 dBm at BLE 2 Mbps. RF figures
  are laboratory module specifications and not a product range guarantee.
- UART supports up to 6 Mbps, including DMA and hardware CTS/RTS. I2C supports
  master/slave operation up to 3.4 Mbps. Two SPI controllers support up to
  48 MHz with DMA. Use only a board-established mux and bus instance.
- USB 2.0 full-speed host/device PHY, I2S, MPI, SD/SDIO/eMMC and LCDC are
  module interfaces. None becomes a Runtime App capability merely by appearing
  in this list.

### Power Planning Values

At 3.8 V and 0 dBm, the datasheet gives typical BLE advertising current of
47.0 uA at a 200 ms interval, 19.3 uA at 500 ms, and 8.9 uA at one second;
BLE connected current is 23.2 uA, 8.9 uA, and 4.1 uA at the same intervals.
These are radio-only deltas, excluding display, sensors, storage and application
work. They are useful only as a starting point for a measured board power budget.

## Design-Time Restrictions From The Vendor Checklist

- Place 4.7 uF plus 0.1 uF near VSYS and 0.1 uF near VBATS when designing a
  carrier. Battery and touch-panel interfaces need ESD protection.
- The internal PCB antenna needs a metal/component/trace keep-out. Prefer the
  antenna to extend beyond the carrier-board edge; otherwise the vendor
  recommends at least 15 mm clearance and removing carrier PCB under it. A
  finished enclosure still requires RF validation.
- The source is a module specification, not a Huangshan Pi carrier-board
  schematic. Its Raspberry Pi, LCD, CTP, SD and key assignments are examples
  for a reference development board and must never overwrite verified board
  mappings above.

## Rules For Future Work

1. Start from the verified board mapping and Runtime capabilities, not a module
   feature list. A source-only feature requires carrier schematic evidence and
   a BSP-level implementation before it reaches Apps.
2. Treat all unclaimed pins as reserved until their net, power state, boot and
   recovery behavior have been checked. In particular, do not touch PA12-PA17,
   PA18/PA19, PA22/PA23, PA34-PA36, display, touch, SPI1 SD, or power pins.
3. For optional sensors, use the probe-first contract in
   `.agents/skills/huangshan-sensor-availability/`; current hardware evidence
   beats historic example logs.
4. Any carrier-board, battery, antenna, high-current, charger, reset, USB, or
   raw bus change needs schematic review and physical validation; it is outside
   normal Runtime App scope.
