# Known issues and safety

**Experimental; public release is on hold for watchdog investigation.** Manual feature checks and code review do not establish broad compatibility, a failure rate or dependable unattended operation.

## Safety

Keep the opener's original controls and manufacturer-specified entrapment protection working. Never bypass photoeyes or other safety devices. Supervise trials, keep the travel area clear and do not rely on OG as an emergency Stop or build unattended-closing automations around this experiment.

The initial warning follows stock OG's onboard buzzer method. The LED is not a safety-warning light. Existing opener-light control is not an automatic warning-light feature. Buzzer-only implementation and firmware testing do not establish product/regulatory compliance; the build must not claim UL 325 compliance. Keep beta/experimental labeling until qualified product-safety review evaluates the complete accessory/operator system.

Unexpected movement, interference with native controls or compromised access is a reason to stop using the affected configuration and report it. Isolate accessory wiring using the opener's safe power/wiring procedure. Keep credentials private and never expose the device directly to the Internet.

## Watchdog resets

Software and hardware watchdog indications have occurred on development installations; the cause and frequency remain unresolved. Read-only monitoring is underway. A reset can lose availability and discard a pending request; commands are not automatically replayed. No watchdog fix or stability pass is claimed.

Report firmware version, opener/panel model, event time, Uptime, Reset Reason and available heap/loop diagnostics. Distinguish deliberate updates/restarts from unexpected events. Reset Reason can be retained and republished without a new reboot; correlate with uptime. Maximum Loop Time is an interval maximum, not proof of uninterrupted watchdog starvation or an exact crash timestamp. Preserve the exact image and matching ELF privately for any later trace decoding.

## Unknown state in Home Assistant

Before real status arrives, door state is Unknown. ESPHome 2026.8.2's native cover message cannot express missing position, so HA may show an **unverified Open placeholder** or retain an earlier indication. Use **Door State / Door State Valid** for state-sensitive automations. Fabricating Closed could hide an actually open door; NaN is not a supported fix.

A known moving state can be valid telemetry without being eligible for another directional Open/Close request. The reason `Required state unknown` can mean that no eligible stationary state is available. Opener Light also has startup/retention display limitations; use Opener Light State when validity matters.

## Compatibility and remaining limits

- Basic Security+ 1.0 and 2.0 operation and a Security+ 1.0 smart-panel setup have been owner-tested, not a broad model matrix. The special `37` panel is unsupported for active commands.
- Security+ 1.0 obstruction decoding follows stock's explicitly unproven interpretation. Obstruction telemetry is not a replacement safety interlock. Remote Lock does not guarantee RF inhibition on every model.
- Security+ 2.0 retains an experimental zero-on-boot rolling counter with stable per-device identity. Queries and actions advance the same 28-bit sequence. Observed responses do not prove rolling-code acceptance, and encoder wrap does not prove opener acceptance across wrap.
- A warning may finish without dispatch if the protocol bus remains busy. Check Last Action Reason; no delayed door press or automatic motion retry is queued.
- Station Wi-Fi loss during a network-originated dry-contact pulse can shorten the contact closure. It may already have registered or may be missed; cancellation cannot undo a dispatched command.
- Sensor mounting and filtering can produce delayed or misleading-looking state. Unknown Toggle remains possible, but that does not turn it into a reliable Stop.
- Preference layout changes between firmware profiles can revert calibration to defaults. Verify thresholds after migrations even if a checksum prevents unchecked bytes being interpreted as a distance.
- The main web dashboard loads ESPHome frontend assets from a CDN; browser Internet access is needed. The setup/update page is self-contained. Frontend changes can affect presentation independently of firmware.
- There is no direct MQTT support, percentage positioning, dedicated Stop, stock OTC/cloud integration or automatic Device Builder adoption. HA's native ESPHome integration is the supported connection.
- Early boot failures may require serial recovery; see [recovery](recovery.md). First setup has a ten-minute open-AP window; work in a trusted location and save your generated credentials.

## Report a problem or working configuration

Use the repository's firmware-report issue template after publication. Include version, hardware, protocol, opener/panel model, expected/observed behavior, timestamps and relevant diagnostics. No extra door cycles are needed merely to report. Redact Wi-Fi details, local addresses, device identifiers and credentials from logs/screenshots. Never attach private-details downloads, personalized firmware/ELFs or flash backups.

