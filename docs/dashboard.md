# Optional Home Assistant cards

The [Controls](../examples/home-assistant/controls-card.yaml) and [Sensors](../examples/home-assistant/sensors-card.yaml) templates provide ordered everyday panels using built-in HA Entities cards. They do not require HACS or a custom integration, and firmware discovery does not install them automatically.

1. Open a dashboard, select Edit dashboard, then Add card → Manual.
2. Paste one template into one card.
3. Replace every `replace_me_*` entity ID with the corresponding entity from your OG device. Use the entity picker or device entity settings; IDs vary between installations.
4. Save, then repeat for the other card if desired.

The Controls template contains Garage Door, Request Door Toggle, Cancel Pending Action, Opener Light and Remote Lock. Sensors contains Door Open, Door State, Distance, Vehicle Present, Vehicle State and Opener Light State. Use the read-only Opener Light State binary sensor, not the controllable light, for the sensor row.

In dry-contact mode, omit the light/lock rows and their divider, and omit Opener Light State. The static templates do not hide unsupported entities automatically. These layouts do not change HA's device-page sorting or firmware command behavior. Cancel is not Stop, and the cover startup placeholder is not verified state; read [operation](operation.md) and [known issues](known-issues.md).

