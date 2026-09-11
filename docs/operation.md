# Controls and settings

## Hardware and protocol

The firmware reads GPIO10 to select **v2.0–v2.2** or **v2.3+** behavior. Legacy hardware automatically uses dry contact and hides Security+ settings. On v2.3+, select **None**, **Security+ 1.0**, or **Security+ 2.0**, then restart to apply. This is not automatic protocol detection. Inconsistent hardware detection inhibits control rather than guessing a fallback.

For Security+ 1.0, automatic panel detection listens initially for an existing smart panel before deciding whether to emulate one. The initial listening interval is 20 seconds. Consult **Sec+ 1.0 Panel Mode** for the result. Attach the intended panel before restarting; do not add one during active emulation. The special `37` panel is unsupported for active control. Protocol and panel-setting changes require restart.

Light, remote lock and obstruction are not supported in dry-contact mode. HA may retain previously discovered entities as unavailable after a mode change.

## Door actions

- **Garage Door Open/Close** requests a direction using verified state. Security+ 2.0 also accepts a confirmed Stopped state; Security+ 1.0 and dry-contact direction requests require an eligible Open/Closed endpoint.
- **Request Door Toggle** behaves like an opener button and can be requested even when door state is unknown, subject to the other hardware, setup, transport and network guards. It bypasses the 30-second directional-command cooldown.
- Door requests normally sound a five-second buzzer warning. A Toggle during fresh, confirmed Security+ Opening/Closing skips the warning. Dry-contact Toggle always warns. A warning already underway is not shortened when motion is subsequently detected.
- **Cancel Pending Action** cancels before dispatch. It cannot undo a sent command or stop door movement.

Toggle may stop, reverse or start the door depending on the opener and its current state. It is **not a dedicated or emergency Stop**. There is no percentage-position control. Open/Close normally retain the 30-second cooldown; Security+ 2.0 direction from confirmed Stopped is an exception and still warns.

The onboard button works without station Wi-Fi during ordinary operation; remote requests require the station connection. Explicit recovery/update modes inhibit control. See [button gestures](recovery.md).

## Light, lock and obstruction

**Opener Light** is independent of door endpoint state and travel cooldown, but still requires a ready protocol/control path. Requested targets do not establish that the opener accepted a command; check reported state.

**Remote Lock** controls the opener's remote-control lock mode. It is not a mechanical deadbolt and does not disable HA/OG commands. Missing/stale lock reports remain Unknown. The web buttons highlight the reported state, not just the last request.

**Obstruction** is read-only protocol telemetry, not a replacement for photoeyes or an added safety interlock. The Security+ 1.0 interpretation remains unverified; see [known issues](known-issues.md).

## Distance and vehicle calibration

In HA, open the OG device's **Configuration** section; on the device website use **Garage Configuration**.

| Setting | Range | Applies to |
| --- | --- | --- |
| Vehicle Distance Threshold | 0–450 cm | Ceiling-mounted distance sensing, including Security+ modes; 0 disables vehicle inference |
| Door Open Distance Threshold | 1–450 cm | Distance-based dry-contact door sensing |

For ceiling mounting, a valid distance at or below the door threshold means Open. Valid unobscured distance at or below the vehicle threshold means Present. Security+ door status comes from the protocol and does not override distance-based vehicle inference. Position the sensor so it actually sees the intended door/vehicle surfaces.

Edits apply without recompilation or restart and are saved on OG. They are refused during incompatible active operations; valid changes cancel pending warnings. Calibration never sends a door command, but may change states used by HA automations. Use occasional calibration edits, not continuous automated flash writes.

Firmware distances are in centimeters; HA may display converted units according to its configuration. Saved thresholds override YAML defaults. Verify/reapply calibration after firmware migrations: incompatible preference records can fall back to defaults rather than retain calibration.

The generic configuration samples every 500 ms and uses the tightest five of the latest seven samples within the configured consensus range. Invalid or stale measurements can produce Unknown. Filtering adds latency and cannot fix incorrect mounting. Mounting, contact-input, filter and LED choices remain build-time settings; the blue status LED is off by default.

Startup tunes distinguish AP availability from a connected station. Successful first-use AP-to-station setup has its own short melody. Door warnings take priority over diagnostic/startup audio.

