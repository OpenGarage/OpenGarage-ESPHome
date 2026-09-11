# OpenGarage for ESPHome

ESPHome-based firmware for OpenGarage (OG) with local Home Assistant (HA) integration. Supports **OpenGarage v2.0–v2.2 and v2.3+ boards with 4 MB flash**. Older 2 MB boards are not supported.

**Experimental, not yet publicly released.** This firmware is intended for user testing. Basic operation has been manually tested, but broad opener compatibility and long-term reliability have not been established. Read the [known issues and safety limits](docs/known-issues.md).

## Start Here

To install: download the release `.bin`, upload it to OG, configure Wi-Fi through its setup AP, and add the discovered ESPHome device to Home Assistant. No compilation, MQTT broker, HACS or ESPHome Device Builder is required to use a prebuilt image. This firmware replaces the stock UI and stock cloud integrations.

- [Installation and HA Pairing](docs/install.md)
- [Controls and Operation](docs/operation.md)
- [Updates and Recovery](docs/recovery.md)
- [Optional HA Cards](docs/dashboard.md)
- [Known Issues](docs/known-issues.md)
- [Building and Contributing](docs/development.md)

## Features and Hardware

The v2.0–v2.2 hardware uses dry-contact control. On v2.3+, select None / Security+ 1.0 / Security+ 2.0 in software and restart. Hardware detection does not automatically detect the opener protocol or establish wiring compatibility.

Features include door controls, distance-based vehicle sensing, editable distance thresholds, stock-style buzzer warnings, audible IP reporting, Wi-Fi/factory reset gestures, and Security+ light, remote-lock and reported obstruction entities. Keep the opener's original controls and safety devices operational; Toggle is not an emergency Stop.

## Source and License

Project code is GPL-3.0-or-later. See [LICENSE](LICENSE) and [VENDOR.md](VENDOR.md) for dependency provenance and retained third-party notices.
