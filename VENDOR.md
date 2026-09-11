# License and Dependency Provenance

Project code is GPL-3.0-or-later. Preserve [LICENSE](LICENSE) and upstream notices when redistributing. Dependency licenses remain their own; this project's license does not relicense ESPHome or its dependencies.

## Imported Security+ Codec

The following files in `components/opengarage_secplus_codec/` are unchanged imports from [argilo/secplus revision c35de10b1da5ad97c996402ea9ba18a65932a3cc](https://github.com/argilo/secplus/tree/c35de10b1da5ad97c996402ea9ba18a65932a3cc). The source and header retain Copyright 2022 Clayton Smith and GPL-3.0-or-later notices.

| File | Upstream path | SHA-256 |
| --- | --- | --- |
| `secplus.c` | `src/secplus.c` | c0bc43928cd2919fc6de925a442bf1d03a6f0290bfe42c82172a2534a6444074 |
| `secplus.h` | `src/secplus.h` | e3a7ab79723c653d592c673e1eddaf5e2de5421a04b36b66ae476b7f28f17749 |
| `COPYING` | `COPYING` | 3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986 |

The codec is compiled once as C and used for Security+ 2.0 encoding/decoding. Local transport, framing, scheduling and control adapters are separate implementations.

## Behavior References

These immutable revisions record prior design comparisons, not claims about current upstream HEAD:

- [OpenGarage Firmware](https://github.com/OpenGarage/OpenGarage-Firmware/tree/7cb566f53b1da7be20579d56a83521ab73176527): pins, sensing and stock behavior.
- [OpenGarage Garagelib](https://github.com/OpenGarage/garagelib/tree/6ae27219ed43c80c3b85ad60f2cd1f9f94331cfd): Security+ framing, commands and status interpretation.
- [esphome-ratgdo](https://github.com/ratgdo/esphome-ratgdo/tree/db22ddfcafcc0663fb7857cbe2bff360fd2703c7): protocol timing/state behavior; no copied runtime code or blanket license compatibility inferred.
- [konnected-esphome](https://github.com/konnected-io/konnected-esphome/tree/c0d0c3ad66f7cbfde877b8bcda259c533334be9b) and [gdolib](https://github.com/konnected-io/gdolib/tree/6a9dcf2443f04abbe3a4e22d670a4d9d1d478239): design/protocol comparisons, not bundled implementations.

New Security+ 2.0 IDs use Konnected's [Re-sync pattern](https://github.com/konnected-io/konnected-esphome/blob/master/packages/secplus-gdo.yaml), reviewed September 11, 2026: `((random & 0x7F7F) << 16) | 0x2908`. Existing stored identities are preserved. This is a generation convention, not a claimed universal protocol requirement.

## Build Dependencies

ESPHome 2026.8.2, ESP8266 PlatformIO platform 4.2.1 and Arduino core 3.1.2 are pinned. EspSoftwareSerial 8.0.1 is supplied by that core under its upstream LGPL-2.1-or-later notices; no floating fork is fetched. Other networking/framework libraries are resolved by ESPHome/PlatformIO and retain their licenses. Preserve dependency notices and corresponding source obligations when distributing binaries.

When updating imported code, select an immutable canonical revision, check its per-file license, retain notices and review the diff. Record hashes and rerun affected tests/builds. Never import generated `.pio/libdeps` files or a dirty local copy as an authoritative upstream snapshot.
