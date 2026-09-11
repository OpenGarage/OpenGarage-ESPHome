# Install and pair with Home Assistant

These are candidate instructions; public release is on hold. Use only a matching, maintainer-approved **generic ESP8266 application image**, not a personalized or CI-test image. Read [known issues and safety](known-issues.md) and [recovery](recovery.md) first.

## Install

1. Confirm supported OG v2.x hardware with 4 MB flash. Save your existing firmware/settings and private credentials, including any stock OTC token. Keep physical serial recovery possible.
2. Check the download against its published SHA-256 checksum. Upload the `-esp8266-ota.bin` application through stock OG's Firmware Update page using the stock device key. Installation replaces the stock firmware, UI and cloud features. Do not upload an ELF or full-flash backup.
3. After reboot, join the uniquely suffixed OpenGarage setup Wi-Fi network and open **http://192.168.4.1/**. Initial setup uses an open AP for ten minutes after power-up; work in a trusted location. Power-cycle to reopen the window if needed.
4. Select your 2.4 GHz network and enter its password. Use manual network entry for hidden/unlisted networks. Refresh reloads available scan results; it does not force a fresh scan.
5. Click **Save and connect**. Save/download the generated admin password, HA encryption key and Security+ 2.0 client ID. Then click **I've saved these — connect** to commit settings and restart. The preview itself does not save settings.
6. Rejoin your home Wi-Fi and open the displayed `http://opengarage-xxxxxx.local/` address. If name discovery fails, find the device in your router's client list or use its [audible IP report](recovery.md). Settings saved does not by itself confirm connection to the new network.

If the network details are wrong, the fallback AP normally returns after about 90 seconds. Once provisioned, it uses your generated admin password; the open initial AP is not the normal recovery network. Outputs remain inhibited during first-use setup and explicit Wi-Fi recovery.

## Home Assistant

In **Settings → Devices & services**, add the discovered ESPHome device and paste the saved HA encryption key. If discovery fails, add the ESPHome integration manually using OG's IP address (native API port **6053**).

The key can also be retrieved from `http://DEVICE-IP/og/setup`, using username **admin** and your generated password. Keep the downloaded private-details file secret. Each device generates its own credentials; do not share one device's details with another.

Check **Hardware Version** and **Protocol Configuration**, select the appropriate protocol where supported, restart, and verify sensing, calibration and warning audibility before supervised use. See [operation](operation.md). The device page is usable immediately; the [dashboard templates](dashboard.md) are optional and must be added manually.

## Device web interface

The homepage shows controls, sensors, garage configuration and diagnostics. **Open setup** leads to `/og/setup` for HA pairing details, firmware uploads and Wi-Fi administration. Both use the same admin authentication. A browser may cache authentication, so repeated password prompts are not guaranteed; close the browser session or use a private window when using a shared computer.

To change Wi-Fi while connected, enter the new SSID and password under **Change WiFi Settings**. Station mode uses manual entry; AP setup/recovery offers the network list. Follow the displayed `.local` address after reconnecting to the new network, or consult the router. Never expose the device's web/API/OTA ports directly to the Internet. Use securely configured HA remote access instead.

