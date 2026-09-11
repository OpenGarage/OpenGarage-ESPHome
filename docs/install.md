# Install and Pair with Home Assistant

## Hardware Setup

Follow the stock OpenGarage User Manual's [Hardware Setup section](https://opengarage.github.io/OpenGarage-Firmware/1.2.4/manual/#hardware-setup) for wiring and mounting instructions. The OpenGarage-ESPHome firmware replaces the stock software setup and built-in web interface.

## Install

1. Download the file ending in `-esp8266-ota.bin` from [GitHub Releases](https://github.com/OpenGarage/OpenGarage-ESPHome/releases), under **Assets**. Check the downloaded file against its published SHA-256 checksum. Upload the `.bin` file through the stock OG firmware's **Firmware Update** page. Installation replaces the stock firmware, UI and cloud features.
2. After reboot, join the OpenGarage Wi-Fi access point (AP). Its name looks like `opengarage-xxxxxx`, where `xxxxxx` is the last six characters of its MAC address, excluding colons. Then open a browser and navigate to **http://192.168.4.1/**. Initial setup uses an open AP for ten minutes after power-up; work in a trusted location. Power-cycle to reopen the window if it has expired.
3. Select the 2.4 GHz Wi-Fi network you want OpenGarage to connect to and enter its password. Use manual network entry for hidden/unlisted networks. Click **Save and connect**.
4. **IMPORTANT:** Click **Download private details** to save the admin password, HA encryption key and Security+ 2.0 client ID. Keep this file private. Then click **I've saved these — connect** to save the settings and restart OpenGarage.
5. Join the same Wi-Fi network you configured OpenGarage to use and open the displayed `http://opengarage-xxxxxx.local/` address. If name discovery fails, find the device in your router's client list or use the [Audible IP Report](recovery.md).

**NOTE:** If the network details are wrong, the fallback AP normally returns after about 90 seconds. Once provisioned, it uses your generated admin password; the open initial AP is not the normal recovery network. Outputs remain inhibited during first-use setup and explicit Wi-Fi recovery.

## Home Assistant

In **Settings → Devices & Services**, add the discovered ESPHome device and paste the saved HA encryption key. If discovery fails, add the ESPHome integration manually using OG's IP address (native API port **6053**).

The key can also be retrieved from `http://DEVICE-IP/og/setup`, using username **admin** and the generated password in the downloaded private-details file. Each device generates its own credentials; do not reuse one device's credentials on another.

Check **Hardware Version** and **Protocol Configuration**. If you have OpenGarage v2.3+, use **Opener Protocol** to select the protocol that matches your garage door opener. Restart, then verify sensing, calibration and warning audibility before supervised use. See [Operation](operation.md). The device page is usable immediately; the [Dashboard Templates](dashboard.md) are optional and must be added manually.

## Device Web Interface

The homepage shows controls, sensors, garage configuration and diagnostics. **Open setup** leads to `/og/setup` for HA pairing details, firmware uploads and Wi-Fi administration. Both pages use the same admin authentication. A browser may cache authentication, so repeated password prompts are not guaranteed; close the browser session or use a private window when using a shared computer.

To change Wi-Fi while connected, enter the new SSID and password under **Change WiFi Settings**. Station mode uses manual entry; AP setup/recovery offers the network list. Follow the displayed `.local` address after reconnecting to the new network, or consult the router. Never expose the device's web/API/OTA ports directly to the Internet. Use securely configured HA remote access instead.
