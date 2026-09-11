# OpenGarage for ESPHome

Alternative firmware for OpenGarage with local Home Assistant integration. One generic ESP8266 image supports **OpenGarage v2.0–v2.2 and v2.3+ with 4 MB flash**. Older 2 MB boards and ESP32-C3 hardware are not supported.

**Experimental, not yet publicly released.** Release is currently on hold while unexplained watchdog resets are investigated. Basic operation has been manually tested, but broad opener compatibility and dependable unattended operation are not established. Read the [known issues and safety limits](docs/known-issues.md).

## Start here

Ordinary users will download the release `.bin`, upload it to OG, configure Wi-Fi through its setup AP, and add the discovered ESPHome device to Home Assistant. No compilation, MQTT broker, HACS or ESPHome Device Builder is required to use a prebuilt image. This firmware replaces the stock UI and stock cloud integrations.

- [Installation and Home Assistant pairing](docs/install.md)
- [Controls, protocol settings and calibration](docs/operation.md)
- [Updates, button gestures and return to stock](docs/recovery.md)
- [Optional Home Assistant cards](docs/dashboard.md)
- [Known issues, safety and reporting problems](docs/known-issues.md)
- [Building and contributing](docs/development.md)

The v2.0–v2.2 hardware uses dry-contact control. On v2.3+, select None / Security+ 1.0 / Security+ 2.0 in software and restart. Hardware detection does not automatically detect the opener protocol or establish wiring compatibility.

Features include door controls, distance-based vehicle sensing, editable distance thresholds, stock-style buzzer warnings, audible IP reporting, Wi-Fi/factory reset gestures, and Security+ light, remote-lock and reported obstruction entities. Keep the opener's original controls and safety devices operational; Toggle is not an emergency Stop.

## Source and license

Project code is GPL-3.0-or-later. See [LICENSE](LICENSE) and [VENDOR.md](VENDOR.md) for dependency provenance and retained third-party notices.

This publication tree retains filtered code history. Internal test evidence, exhaustive regression suites, private deployment records and obsolete build profiles are not included. Historical commits are development snapshots, not individually supported buildable releases; use the current tree or a published release tag. No release tag or downloadable binary is published by this preparation step.

