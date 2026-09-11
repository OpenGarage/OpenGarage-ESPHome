# Updates and Recovery

Keep your private credentials and settings backed up. OTA recovery requires running firmware and a reachable device. If your OG cannot boot or its firmware is corrupted, physical serial access may be necessary.

## Firmware Updates

1. Click **Open setup** on the device homepage, or navigate to `http://DEVICE-IP/og/setup`. If prompted to log in, use username **admin** and the password in the private-details file you downloaded during setup.
2. Under **Firmware Update**, click the button to enter update mode. Controls remain inhibited until restart.
3. Select and upload a compatible ESP8266 application `.bin` file—either stock OG or OpenGarage-ESPHome firmware. Do not upload an ELF or full-flash backup. **100% uploaded means transfer completion, not successful installation.** Wait for the accepted/restarting message, then reconnect and verify the firmware version.
4. If the result is **Update not confirmed**, check the device before retrying. Do not interrupt an active upload or assume a lost connection means failure.

If update mode was entered accidentally and no upload is active/pending, use **Restart Device** or power-cycle to resume. Cancel Pending Action does not exit update mode. There is no automatic HA firmware-download/update integration in this release.

Ordinary generic OTA is designed to retain settings and credentials; this is not a guarantee across arbitrary development builds or stock migrations.

## Physical Button

The onboard button provides the same basic functions as [stock OG firmware](https://opengarage.github.io/OpenGarage-Firmware/1.2.4/manual/#step-4-button-actions). Use these hold times, then release:

| Click/Hold | Action |
| --- | --- |
| Short click (under 0.8 s) | Door Toggle |
| Hold 1–3 s, then release | Audible station IP report |
| Hold 5–8 s, then release | Wi-Fi reset to recovery AP |
| Hold more than 10 s, then release | Factory reset |

A low beep marks the Wi-Fi-reset threshold (about 5 s); a higher beep marks factory reset (about 10 s). Release after the appropriate beep. Crossing a threshold alone does not reset. Do not hold the button while power-cycling: doing so causes the controller to enter bootloader mode.

Pressing the button during a pending door warning cancels it; releasing that cancellation press does not issue another action. Continuing the hold through the reset thresholds still selects recovery.

### Audible IP Discovery

Hold the button for about 1–3 seconds, release, and count groups of notes: 1–9 notes indicate that digit; ten notes mean zero. A higher tone marks each dot and a final higher tone ends the address. A long address can take a minute or more. Wi-Fi loss, actual door actions, recovery and updates can interrupt playback; changing door status alone does not.

### Change Wi-Fi Settings

While OG is connected, open `http://DEVICE-IP/og/setup` and use **Change WiFi Settings**. If OG can no longer connect to Wi-Fi, use the button's Wi-Fi reset gesture. Then join the recovery AP using your existing admin/recovery password. Open **http://192.168.4.1/** and log in as **admin** with the same password. Save new Wi-Fi details and restart. Admin password, HA key, client identity, protocol and thresholds are retained. Controls stay inhibited during this explicit recovery boot.

If you have lost the recovery password or want to clear the saved ESPHome settings, use factory reset.

### Factory Reset / Forgotten Password

Factory reset clears ESPHome preferences and returns to first-use setup. Save the new credentials, update HA pairing, select the protocol and restore calibration. Security+ 2.0 gets a fresh client ID; ordinary reboot, OTA and Wi-Fi-only reset preserve it.

Factory reset does not replace firmware or perform an all-flash secure wipe. Historical stock regions are not explicitly erased. Physical reset requires a running application and is blocked during active firmware upload.

New Security+ 2.0 client IDs follow Konnected's pattern: `((random & 0x7F7F) << 16) | 0x2908`. The fixed suffix is hexadecimal; the decimal ID displayed during setup will not necessarily end in 2908. Existing saved IDs, including older fully random IDs, are preserved on updates. Random generation does not guarantee a different ID after every factory reset.

## Return to Stock Firmware

Obtain stock firmware from the official [OpenGarage Firmware Downloads](https://opengarage.github.io/OpenGarage-Firmware/).

Upload the stock application through the guarded update procedure above. Confirm actual stock boot, reconfigure Wi-Fi/settings as needed and verify opener mode before use. Keep control wiring isolated for installed-door recovery work using the opener's safe wiring procedure; stock firmware can become active after boot. This is an installation precaution, not an OTA transport requirement.

To return to ESPHome, upload the generic ESP8266 application through stock's Firmware Update page. Do not assume settings survive either direction.
