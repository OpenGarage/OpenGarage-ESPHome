# Controls and Settings

## Hardware and Protocol

For wiring and mounting, follow the stock OpenGarage User Manual's [Hardware Setup section](https://opengarage.github.io/OpenGarage-Firmware/1.2.4/manual/#hardware-setup). This guide covers the ESPHome controls and settings that replace the stock software setup and web interface.

The firmware detects whether your OG is v2.0–v2.2 or v2.3+ by reading the state of `GPIO10`.

- **Legacy (v2.0–v2.2):** Supports only dry-contact control. Security+ settings, opener light, remote lock and obstruction entities are hidden.
- **v2.3+:** Supports dry-contact and Security+ control. Under **Opener Protocol**, select **None** (dry contact), **Security+ 1.0**, or **Security+ 2.0** to match your garage door opener, then restart to apply. Light, remote lock and obstruction are unavailable in dry-contact mode.

Hardware detection does not automatically detect the opener's protocol. Inconsistent hardware readings inhibit control rather than selecting a fallback. HA may retain previously discovered entities as unavailable after a mode change.

**Panel Emulation:** For Security+ 1.0, automatic panel detection listens initially for an existing smart panel before deciding whether to emulate one. The initial listening interval is 20 seconds. Consult **Sec+ 1.0 Panel Mode** for the result. Attach the intended panel before powering on OG; do not add one during active emulation. The special `0x37` panel is unsupported for active control. Protocol and panel-setting changes require restart.

With an existing Security+ 1.0 smart panel, light/lock commands and warned door requests may wait briefly for a suitable gap in panel traffic. If the bus remains busy, a request can expire without being sent—even after the warning sounds. A Toggle during confirmed motion does not wait: it can be refused if no suitable gap is available immediately. Transmitted toggles are not automatically retried.

Some smart panels leave insufficient time for OG commands. **Sec+ 1.0 Panel Mode** displays **Panel timing insufficient for commands** when observed timing establishes this limitation. To use OG without the incompatible panel, set **Panel Emulation** to **Automatic**, power off the opener and OG before disconnecting the smart panel, then restore power and restart OG. Allow the initial listening period to finish; Panel Mode should change to **Emulating wall panel**. Keep the opener's safety devices connected.

## Door Actions

If a Security+ 2.0 opener stops responding, OG retries status queries automatically. The **Sec+ 2.0 Session** diagnostic shows **No response; waiting to retry** between attempts. Waits increase from 30 seconds to a five-minute cap; controls remain unavailable until responses are observed again. Pending actions are not replayed. A **TX failed; reboot required** message is a separate fault and still requires a restart.

- **Garage Door Open/Close** requests a direction using the reported door state. Requesting Open when already Open, or Close when already Closed, does nothing. Directional requests require a known Open/Closed state; Security+ 2.0 also permits either direction from a confirmed Stopped state. They are not accepted while state is Unknown or the door is moving. A **30-second cooldown** starts when a door command is sent and normally blocks further Open/Close requests.
- **Request Door Toggle** behaves like an opener button and can be requested even when door state is unknown, subject to the other hardware, setup, transport and network guards. It bypasses the 30-second directional-command cooldown.
- Door requests normally sound a five-second buzzer warning. A Toggle during fresh, confirmed Security+ Opening/Closing skips the warning. Dry-contact Toggle always warns. A warning already underway is not shortened when motion is subsequently detected.
- **Cancel Pending Action** cancels before a command is dispatched. It cannot undo a sent command or stop door movement.

Toggle may stop, reverse or start the door depending on the opener and its current state. It is **not a dedicated or emergency Stop**. There is no percentage-position control. Toggle and Security+ 2.0 Open/Close from confirmed Stopped bypass the cooldown; commands from Stopped still sound the warning. Pending actions and short repeat-command guards can still prevent another request.

**The Onboard Button** triggers a Toggle and works without Wi-Fi during ordinary operation, subject to the same hardware and control-readiness checks. HA/web requests require OG's station Wi-Fi connection. Explicit recovery/update modes inhibit control. See [Button Gestures](recovery.md).

## Light, Lock and Obstruction

**Opener Light** can be controlled while the door is moving or its position is unknown, independently of the door-command cooldown. It still requires a ready protocol/control path. A request does not confirm that the opener accepted the command; check the reported light state.

**Remote Lock** controls the opener's remote-control lock mode. It is not a mechanical deadbolt and does not disable HA/OG commands. Missing/stale lock reports remain Unknown. The web buttons highlight the reported state, not just the last request.

**Obstruction** is read-only protocol telemetry, not a replacement for photoeyes or an added safety interlock. Protocol-specific limitations are listed in [Known Issues](known-issues.md).

## Distance and Vehicle Calibration

In HA, open the OG device's **Configuration** section; on the device homepage, use **Garage Configuration**.

| Setting | Range | Applies to |
| --- | --- | --- |
| Vehicle Distance Threshold | 0–450 cm | Ceiling-mounted distance sensing, including Security+ modes; 0 disables vehicle inference |
| Door Open Distance Threshold | 1–450 cm | Distance-based dry-contact door sensing |

For ceiling-mounted, distance-based door sensing, a valid distance at or below the door threshold means Open. A valid, unobscured distance at or below the vehicle threshold means Vehicle Present; a greater distance means Absent. In dry-contact distance mode, vehicle state is Unknown while the open door blocks the sensor. Security+ door status comes from the protocol and does not override distance-based vehicle inference. Position the sensor so it actually sees the intended door/vehicle surfaces.

Edits apply without recompilation or restart and are saved on OG. They are refused during incompatible active operations; valid changes cancel pending warnings. Calibration never sends a door command, but may change states used by HA automations. Use occasional calibration edits, not continuous automated flash writes.

Firmware distances are in centimeters; HA may display converted units according to its configuration. Saved thresholds override YAML defaults. Verify/reapply calibration after firmware migrations: incompatible preference records can fall back to defaults rather than retain calibration.

The generic configuration samples every 500 ms and averages the tightest group of five of the latest seven samples, provided their spread is within the configured consensus range. Invalid or stale measurements can produce Unknown. Filtering adds latency and cannot fix incorrect mounting. Mounting, contact-input, filter and LED choices remain build-time settings; the blue status LED is off by default.

The HA Garage Door cover can show an unverified Open placeholder before real status arrives. Use **Door State / Door State Valid** for state-sensitive automations; see [Known Issues](known-issues.md).

## Startup Sounds

Startup tunes distinguish AP availability from a connected station. Successful first-use AP-to-station setup has its own short melody. Door warnings take priority over diagnostic/startup audio.
