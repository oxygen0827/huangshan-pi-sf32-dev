# SF32LB52 Module Material

This directory stores raw vendor material for the LCSC open SF32LB52 module.
The original PDF is intentionally kept here unchanged so future hardware checks
can refer back to the source document.

Project interpretation lives in:

- `../huangshan-networking.md`

Current conclusion: the downloaded module datasheet documents dual-mode
Bluetooth/BLE radio capability, not built-in Wi-Fi. The current Runtime uses
BLE GATT App install as the default phone-facing transport. Treat Wi-Fi as
unconfirmed unless a carrier-board schematic proves an external Wi-Fi module is
present.
