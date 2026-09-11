# Build and contribute

Users of a released generic binary do not need a compiler. For customization, clone the complete source tree; its YAML uses local packages/components and is not a standalone downloadable configuration.

Use Python 3.12 and a supported ESPHome build environment:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
make check
make compile
```

The sole public entrypoint is `firmware/opengarage-generic.yaml`. No secrets file is needed: credentials are created on the device at setup. The build output is normally under `firmware/.esphome/build/opengarage/.pioenvs/opengarage/`; use its ESP8266 application `firmware.bin` for OTA, not an ELF/full-flash image. Preserve the corresponding source revision, binary checksum and matching ELF when diagnosing crashes. Timestamp/dependency resolution can prevent byte-identical rebuilds.

CI checks repository links/includes, validates this entrypoint and compiles it. It does not upload firmware, operate devices, publish binaries or constitute hardware/safety validation. The exhaustive internal regression suite and deployment utilities are deliberately excluded from the public tree. Contributions should include focused reproducible checks for changed behavior; maintainers also validate changes against their internal suite.

## Architecture

The generic entrypoint includes `read-only.yaml` and `unified-entities.yaml`; the latter includes thresholds and reset diagnostics. `components/opengarage` owns sensing, hardware selection, protocol scheduling, actuation, provisioning and web UI extensions. `components/opengarage_secplus_codec` provides the attributed Security+ codec. Shared source retains internal development branches to avoid a release-preparation refactor; no extra public build profiles are required.

## Pinned framework contracts

ESPHome **2026.8.2**, Arduino **3.1.2**, ESP8266 platform **4.2.1** are pinned. Do not simply relax the guard when upgrading:

- Wi-Fi reset depends on ESP8266's allocation-ordered preference storage. Recheck `SavedWifiSettings` layout (33+65=98 bytes), no-compiled-STA type `88491487UL`, backend offset/checksum semantics, and codegen order: protocol → thresholds → generic credentials → Wi-Fi alias binding → Wi-Fi allocation → later preferences. The alias must not allocate or overwrite another slot.
- Preserve generic validation rejecting compiled Wi-Fi credentials, fast_connect, manual_ip and unsupported MQTT/Improv/provisioning paths. Confirm reset validates the current network before invalidating its record; a valid empty SSID is not an equivalent erase.
- Recheck Wi-Fi-only reset preservation and factory reset clearing on recoverable hardware, including storage failure paths. Never reset an installed opener without coordination.
- Recheck native cover missing-state limits, API subscription behavior, OTA authentication and callback ordering. In the pinned version OTA success and failure both use HTTP 200, so response-body checks matter.
- Recheck web frontend hooks, GPIO reservations, shared UART ownership, SoftwareSerial/ISR and tone/timer behavior. Preserve bounded output cleanup and guarded update mode.
- Run full maintainer regressions, compile, inspect flash/OTA capacity, and retain the exact artifacts before coordinated hardware verification. Do not run config-generation tests concurrently with compilation in the same build directory.

An ESPHome compile alone does not prove safe storage migration or recovery. See [VENDOR.md](../VENDOR.md) before changing imported dependencies.

