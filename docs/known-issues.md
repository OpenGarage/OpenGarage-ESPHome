# Known Issues and Safety

**This firmware is experimental.** Basic operation has been tested, but compatibility across all opener models and long-term reliability are not established.

## Watchdog Resets

Unexpected software and hardware watchdog resets have occurred during development. The cause and frequency remain unresolved. A reset briefly interrupts operation and discards pending requests; commands are not automatically replayed.

If you encounter a reset, report the firmware version, opener/panel model, event time, **Uptime**, **Reset Reason** and **Maximum Loop Time**. Mention any deliberate restart, update or power interruption. Reset Reason alone does not indicate a new reboot—check whether Uptime also restarted.

## Unknown State in Home Assistant

HA's Garage Door cover may show an **unverified Open placeholder** before status arrives, or retain an earlier position when status is lost. This is an ESPHome cover limitation. Use **Door State / Door State Valid** for state-sensitive automations, not the cover display alone.

## Compatibility Notes

- **Security+ 1.0:** The special `0x37` panel is unsupported for active control. Obstruction reporting follows stock's unverified interpretation; do not treat it as a safety interlock.
- **Security+ 2.0:** The rolling counter currently starts at zero after reboot, with a persistent client ID. This policy remains experimental; report synchronization or command-acceptance problems after restarts.
- **Web dashboard:** The main dashboard requires browser Internet access to load ESPHome's frontend. The setup/update page is self-contained.
- **Recovery:** Some boot failures require serial access. Verify saved calibration after firmware migrations. See [Updates and Recovery](recovery.md).

For command behavior and feature limits—including Toggle versus Stop, cooldowns and Remote Lock—see [Controls and Settings](operation.md). Direct MQTT, stock cloud services and percentage-position control are not included.

## Safety

Keep the opener's original controls and safety devices working; never bypass photoeyes. Supervise trials, keep the travel area clear and do not rely on OG as an emergency Stop or build unattended-closing automations around this experiment. Warnings use the onboard buzzer, not the opener light.

Stop using the affected setup if movement is unexpected or native controls are disrupted. Keep credentials private and never expose OG directly to the Internet.

## Feedback

Report problems or working configurations using the firmware-report issue template. Include your hardware, opener/panel model, firmware version and relevant diagnostics. Redact credentials and network/device identifiers from screenshots or logs; never attach private-details files or flash backups.
