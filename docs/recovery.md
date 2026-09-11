# Updates and recovery

Keep private credentials, settings and an appropriate stock image backed up. OTA recovery requires a running, reachable application; physical serial access may still be necessary.

## Firmware updates

1. Ensure the intended device is idle, the travel area is clear and power/network are stable.
2. Open **Open setup** on the device homepage, or `http://DEVICE-IP/og/setup`, and authenticate as **admin**.
3. Under **Firmware Update**, enter update mode. Controls remain inhibited until restart.
4. Select the matching generic `-esp8266-ota.bin` and upload. **100% uploaded means transfer completion, not successful installation.** Wait for the accepted/restarting message, then reconnect and verify the firmware version.
5. If the result is **Update not confirmed**, check the device before retrying. Do not interrupt an active upload or assume a lost connection means failure.

If update mode was entered accidentally and no upload is active/pending, use **Restart Device** or power-cycle to resume. Cancel Pending Action does not exit update mode. There is no automatic HA firmware-download/update integration in this release.

Ordinary generic OTA is designed to retain settings and credentials; this is not a guarantee across arbitrary development builds or stock migrations. Use only a published artifact matching the hardware. Filename extensions are hints, not hardware validation. Do not substitute a factory/full-flash image, ELF or an ESP32 image in an ESP8266 application uploader.

## Physical button

Press after normal boot, then release:

| Hold time | Action |
| --- | --- |
| More than 50 ms, up to 0.8 s | Door Toggle |
| More than 0.8 s, up to 4.5 s | Audible station IP report |
| More than 4.5 s, up to 9.5 s | Wi-Fi reset to recovery AP |
| More than 9.5 s | Factory reset |

A low beep marks the Wi-Fi-reset threshold; a higher beep marks factory reset. Release after the appropriate beep. Crossing a threshold alone does not reset. Do not hold the button while power-cycling: GPIO0 also selects the serial bootloader. A button already held at boot must first be released.

Pressing during a pending door warning cancels it; releasing that cancellation press does not issue another action. Continuing the hold through the reset thresholds still selects recovery.

### Audible IP

Hold about 1–2 seconds, release, and count groups of notes: 1–9 notes indicate that digit; ten notes mean zero. A higher tone marks each dot and a final higher tone ends the address. A long address can take a minute or more. Wi-Fi loss, actual door actions, recovery and updates can interrupt playback; changing door status alone does not.

### Wi-Fi reset

Join the recovery AP using your existing admin/recovery password. Open **http://192.168.4.1/** and authenticate as admin. Save new Wi-Fi details and restart. Admin password, HA key, client identity, protocol and thresholds are retained. Controls stay inhibited during this explicit recovery boot.

### Factory reset / forgotten password

Factory reset clears ESPHome preferences and returns to first-use setup. Save the new credentials, update HA pairing, select the protocol and restore calibration. Security+ 2.0 gets a fresh client ID; ordinary reboot, OTA and Wi-Fi-only reset preserve it.

Factory reset does not replace firmware or perform an all-flash secure wipe. Historical stock regions are not explicitly erased. Physical reset requires a running application and is blocked during active firmware upload.

## Return to stock

Obtain stock firmware from the [official OpenGarage downloads](https://opengarage.github.io/OpenGarage-Firmware/). The application used for previous round-trip tests was **og_1.2.4.bin**, 564,832 bytes, SHA-256:

```text
224ca85f3566fb9b91265acaad37b26e0b6361088e0a75772a80a5cad88ac08a
```

Upload the stock application through the guarded update procedure above. Confirm actual stock boot, reconfigure Wi-Fi/settings as needed and verify opener mode before use. Keep control wiring isolated for installed-door recovery work using the opener's safe wiring procedure; stock firmware can become active after boot. This is an installation precaution, not an OTA transport requirement.

To return to ESPHome, upload the generic ESP8266 application through stock's Firmware Update page. Do not assume settings survive either direction. Previous exact OTA round trips passed on legacy pulse and v2.3 Security+ 2.0 development images, including unified 0.9.0; these are not a validation of every later generic image, mode or interrupted update.

If the application cannot boot or be reached, use the correct board-specific 3.3 V serial recovery procedure. Do not erase all flash as the first troubleshooting step. Generic firmware disables upstream early safe mode because it bypasses runtime credential initialization, so some early failures require serial recovery.

