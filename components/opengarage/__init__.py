# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only by default; separate bench and dry-contact pulse MVP opt-ins."""
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import pins
from esphome.components import binary_sensor, sensor, text_sensor, button, cover, light, lock, ota, select, number, api
from esphome.components.esphome.ota import ESPHomeOTAComponent
from esphome.components.esp8266.const import require_waveform
from esphome.const import CONF_ID, __version__ as ESPHOME_VERSION

def AUTO_LOAD(config):
    components = ["sensor", "binary_sensor", "text_sensor"]
    if any(key in config for key in ("dev_pulse_control", "pulse_control", "secplus1_control", "secplus2_control", "unified_control")):
        components.append("button")
    if any(key in config for key in ("pulse_control", "secplus1_control", "secplus2_control", "unified_control")):
        components += ["cover", "web_server_base"]
    if "secplus1_control" in config or "secplus2_control" in config or "unified_control" in config:
        components += ["light", "lock"]
    if "dev_secplus2_rx" in config or "unified_control" in config:
        components.append("opengarage_secplus_codec")
    if "unified_control" in config:
        components.append("select")
    if "threshold_controls" in config:
        components.append("number")
    return components
DEPENDENCIES = ["esp8266"]
MULTI_CONF = False
MAX_THRESHOLD_CM = 450  # Keep in sync with ThresholdSettings::MAX_CM.

ns = cg.esphome_ns.namespace("opengarage")
OpenGarageComponent = ns.class_("OpenGarageComponent", cg.Component)
GenericSetup = ns.class_("GenericSetup", cg.Component)
ControlCommandButton = ns.class_("ControlCommandButton", button.Button)
PulseCover = ns.class_("PulseCover", cover.Cover)
Secplus1Cover = ns.class_("Secplus1Cover", cover.Cover)
Secplus1Light = ns.class_("Secplus1Light", light.LightOutput)
Secplus2Cover = ns.class_("Secplus2Cover", cover.Cover)
Secplus2Light = ns.class_("Secplus2Light", light.LightOutput)
RemoteLock = ns.class_("RemoteLock", lock.Lock)
UnifiedCover = ns.class_("UnifiedCover", cover.Cover)
UnifiedLight = ns.class_("UnifiedLight", light.LightOutput)
ProtocolSelect = ns.class_("ProtocolSelect", select.Select)
ThresholdNumber = ns.class_("ThresholdNumber", number.Number)
OpenerProtocol = ns.enum("OpenerProtocol", is_class=True)
PanelEmulation = ns.enum("PanelEmulation", is_class=True)
UpdateModeButton = ns.class_("UpdateModeButton", button.Button)
StateSource = ns.enum("StateSource", is_class=True)
Mounting = ns.enum("Mounting", is_class=True)
ContactType = ns.enum("ContactType", is_class=True)
FilterMode = ns.enum("FilterMode", is_class=True)
TimeoutPolicy = ns.enum("TimeoutPolicy", is_class=True)
SOURCES = {name.lower(): getattr(StateSource, name) for name in ("DISTANCE", "CONTACT", "BOTH_AND", "EITHER_OR", "PROTOCOL")}
MOUNTINGS = {name.lower(): getattr(Mounting, name) for name in ("CEILING", "SIDE")}
CONTACTS = {name.lower(): getattr(ContactType, name) for name in ("NONE", "NORMALLY_CLOSED", "NORMALLY_OPEN")}
FILTERS = {name.lower(): getattr(FilterMode, name) for name in ("CONSENSUS", "MEDIAN")}
TIMEOUTS = {name.lower(): getattr(TimeoutPolicy, name) for name in ("IGNORE", "CAP")}


def _fixed_pin(number, mode):
    def check(value):
        if value["number"] != number or value["inverted"] or value.get("allow_other_uses", False):
            raise cv.Invalid(f"OpenGarage requires exclusive, non-inverted GPIO{number}")
        enabled_modes = {key for key, enabled in value["mode"].items() if enabled}
        if enabled_modes != set(mode.split("_")):
            raise cv.Invalid(f"GPIO{number} requires mode {mode}")
        return value
    return cv.All(getattr(pins, f"internal_gpio_{mode}_pin_schema"), check)


def _add_pins(value):
    value = cv.Schema({}, extra=cv.ALLOW_EXTRA)(value).copy()
    for key, number, mode in (("button_pin", 0, "input_pullup"), ("led_pin", 2, "output"),
                              ("capability_pin", 10, "input_pullup")):
        value.setdefault(key, {"number": number, "mode": mode})
    if cv.boolean(value.get("distance_enabled", True)):
        value.setdefault("trigger_pin", {"number": 12, "mode": "output"})
        value.setdefault("echo_pin", {"number": 14, "mode": "input"})
    if str(value.get("contact_type", "none")).lower() != "none":
        value.setdefault("contact_pin", {"number": 4, "mode": "input_pullup"})
    return value


def _options(value):
    source = value["state_source"]
    generic = "generic_setup" in value
    if generic and (value["hardware"] != "auto" or "unified_control" not in value or "threshold_controls" not in value):
        raise cv.Invalid("Generic setup requires automatic unified hardware and threshold controls")
    if "unified_control" in value:
        if (value["unified_control"]["client_id"] == "provisioned") != generic:
            raise cv.Invalid("client_id: provisioned requires generic_setup, with no compiled client identity")
    if value["hardware"] == "auto" and "unified_control" not in value:
        raise cv.Invalid("Automatic hardware detection requires unified_control")
    if "threshold_controls" in value:
        if not any(key in value for key in ("pulse_control", "secplus1_control", "secplus2_control", "unified_control")):
            raise cv.Invalid("Threshold controls require a production control profile with OTA lifecycle support")
        thresholds = value["threshold_controls"]
        if not value["distance_enabled"] or source in ("contact", "protocol"):
            thresholds["door"]["internal"] = True
        if not value["distance_enabled"] or value["mounting"] == "side":
            thresholds["vehicle"]["internal"] = True
    if "unified_control" in value:
        if ESPHOME_VERSION != "2026.8.2":
            raise cv.Invalid("Unified startup entity exposure is audited for ESPHome 2026.8.2; re-verify before upgrading")
        if value["hardware"] not in ("auto", "v2_3"):
            raise cv.Invalid("Unified control requires auto or fixed v2_3 hardware")
        if any(key in value for key in ("dev_pulse_control", "pulse_control", "dev_secplus1",
                                       "secplus1_control", "dev_secplus2_rx", "dev_secplus2_sync", "secplus2_control")):
            raise cv.Invalid("Unified control exclusively owns its pulse and Security+ backends")
        if source == "protocol" or "door_state" not in value:
            raise cv.Invalid("Unified control requires a non-protocol state_source for None mode and door_state text")
    if "secplus2_control" in value:
        if "dev_secplus2_sync" not in value:
            raise cv.Invalid("Security+ 2.0 controls require explicit dev_secplus2_sync ownership")
        if any(key in value for key in ("dev_pulse_control", "pulse_control", "dev_secplus1", "secplus1_control")):
            raise cv.Invalid("Security+ 2.0 controls cannot share pins with another backend")
    if "dev_secplus2_sync" in value:
        if "dev_secplus2_rx" not in value:
            raise cv.Invalid("Security+ 2.0 queries require dev_secplus2_rx")
        if not 10000 <= value["dev_secplus2_rx"]["status_timeout"].total_milliseconds <= 60000:
            raise cv.Invalid("Active Security+ 2.0 queries require status_timeout between 10s and 60s")
    if "secplus1_control" in value:
        if "dev_secplus1" not in value or value["dev_secplus1"]["mode"] != "emulate_if_needed":
            raise cv.Invalid("Security+ 1.0 controls require dev_secplus1 with explicit emulate_if_needed TX ownership")
        if any(key in value for key in ("dev_pulse_control", "pulse_control", "dev_secplus2_rx")):
            raise cv.Invalid("Security+ 1.0 controls cannot share pins with another backend")
    if source == "protocol" and not any(key in value for key in ("dev_secplus2_rx", "dev_secplus1")):
        raise cv.Invalid("state_source: protocol requires dev_secplus2_rx or dev_secplus1")
    if "dev_secplus1" in value:
        if value["hardware"] != "v2_3":
            raise cv.Invalid("Security+ 1.0 prototype requires v2_3 hardware")
        if any(key in value for key in ("dev_secplus2_rx", "pulse_control", "dev_pulse_control")):
            raise cv.Invalid("Security+ 1.0 prototype cannot share pins with another backend")
        if source != "protocol" or "door_state" not in value:
            raise cv.Invalid("Security+ 1.0 prototype requires protocol state_source and door_state text")
        if value["dev_secplus1"]["mode"] == "passive" and "tx_pin" in value["dev_secplus1"]:
            raise cv.Invalid("Passive Security+ 1.0 must not configure a TX pin")
    if "dev_secplus2_rx" in value:
        if value["hardware"] != "v2_3":
            raise cv.Invalid("Security+ receive-only prototype requires v2_3 hardware")
        if "pulse_control" in value or "dev_pulse_control" in value:
            raise cv.Invalid("Security+ receive-only prototype cannot be combined with pulse control")
        if source != "protocol":
            raise cv.Invalid("Security+ receive-only prototype requires state_source: protocol")
        if "door_state" not in value:
            raise cv.Invalid("Security+ receive-only prototype requires door_state text")
    if source not in ("contact", "protocol") and not value["distance_enabled"]:
        raise cv.Invalid("This state_source requires distance_enabled: true")
    if source not in ("distance", "protocol") and value["contact_type"] == "none":
        raise cv.Invalid("This state_source requires a contact_type")
    if not value["distance_enabled"] and ("trigger_pin" in value or "echo_pin" in value):
        raise cv.Invalid("Do not configure distance pins when distance is disabled")
    if value["contact_type"] == "none" and "contact_pin" in value:
        raise cv.Invalid("Do not claim GPIO4 without a contact_type")
    if value["stale_timeout"].total_milliseconds < 7 * value["sample_interval"].total_milliseconds:
        raise cv.Invalid("stale_timeout must cover at least seven sample intervals")
    if "dev_pulse_control" in value and value["hardware"] != "v2_0_v2_2":
        raise cv.Invalid("M2 pulse bench control currently requires the v2_0_v2_2 profile")
    if "pulse_control" in value:
        if "dev_pulse_control" in value:
            raise cv.Invalid("Select pulse_control OR dev_pulse_control, never both")
        if "door_state" not in value:
            raise cv.Invalid("Pulse MVP requires a door_state text entity for explicit Unknown reporting")
    return value


def _milliseconds(minimum, maximum):
    return cv.All(cv.positive_time_period_milliseconds,
                  cv.Range(min=cv.TimePeriod(milliseconds=minimum), max=cv.TimePeriod(milliseconds=maximum)))


PULSE_BENCH_SCHEMA = cv.Schema({
    cv.Required("bench_mac"): cv.mac_address,
    cv.Optional("warning_time", default="5s"): _milliseconds(5000, 30000),
    cv.Optional("pulse_time", default="1s"): _milliseconds(100, 1000),
    cv.Optional("lockout_time", default="30s"): _milliseconds(5000, 120000),
    cv.Optional("door_pin", default=15): _fixed_pin(15, "output"),
    cv.Optional("buzzer_pin", default=13): _fixed_pin(13, "output"),
    cv.Required("request_toggle"): button.button_schema(ControlCommandButton),
    cv.Required("cancel"): button.button_schema(ControlCommandButton),
    cv.Optional("controls_armed"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
    cv.Optional("control_phase"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Optional("last_action_reason"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Optional("pulse_count"): sensor.sensor_schema(accuracy_decimals=0, state_class="total_increasing", entity_category="diagnostic"),
})

PULSE_MVP_SCHEMA = cv.Schema({
    cv.Required("opener_input"): cv.one_of("dry_contact", lower=True),
    cv.Required("cover"): cover.cover_schema(PulseCover, device_class="garage"),
    cv.Required("state_valid"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
    cv.Required("firmware_update_mode"): button.button_schema(UpdateModeButton, entity_category="config"),
    cv.Optional("warning_time", default="5s"): _milliseconds(5000, 30000),
    cv.Optional("pulse_time", default="1s"): _milliseconds(100, 1000),
    cv.Optional("lockout_time", default="30s"): _milliseconds(5000, 120000),
    cv.Optional("door_pin", default=15): _fixed_pin(15, "output"),
    cv.Optional("buzzer_pin", default=13): _fixed_pin(13, "output"),
    cv.Required("request_toggle"): button.button_schema(ControlCommandButton),
    cv.Required("cancel"): button.button_schema(ControlCommandButton),
    cv.Optional("controls_armed"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
    cv.Optional("control_phase"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Optional("last_action_reason"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Optional("pulse_count"): sensor.sensor_schema(accuracy_decimals=0, state_class="total_increasing", entity_category="diagnostic"),
})


SECPLUS2_COUNTERS = ("rx_bytes", "valid_frames", "status_frames", "decode_errors", "partial_timeouts",
                    "rx_overflows", "ignored_commands", "invalid_status", "rx_high_water", "max_service_time")
SECPLUS2_RX_SCHEMA = cv.Schema({
    cv.Optional("rx_pin", default=5): _fixed_pin(5, "input"),
    cv.Optional("status_timeout", default="360s"): _milliseconds(1000, 600000),
    cv.Required("status_valid"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
    cv.Optional("light_state"): binary_sensor.binary_sensor_schema(),
    cv.Optional("lock_state"): binary_sensor.binary_sensor_schema(),
    cv.Optional("obstruction"): binary_sensor.binary_sensor_schema(device_class="problem", icon="mdi:garage-alert"),
    **{cv.Optional(key): sensor.sensor_schema(accuracy_decimals=0, entity_category="diagnostic",
          state_class="measurement" if key in ("rx_high_water", "max_service_time") else "total_increasing",
          **({"unit_of_measurement": "µs"} if key == "max_service_time" else {})) for key in SECPLUS2_COUNTERS},
})

SECPLUS2_TX_COUNTERS = ("query_writes", "collisions", "deferrals", "tx_errors", "max_tx_time")
SECPLUS2_SYNC_SCHEMA = cv.Schema({
    cv.Optional("tx_pin", default=15): _fixed_pin(15, "output"),
    cv.Required("client_id"): cv.All(cv.hex_uint32_t, cv.Range(min=1, max=0xFFFFFFFF)),
    cv.Required("rolling_code_strategy"): cv.one_of("stock_zero_on_boot", lower=True),
    cv.Required("sync_state"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Required("rolling_code"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Optional("openings"): sensor.sensor_schema(accuracy_decimals=0, entity_category="diagnostic"),
    **{cv.Optional(key): sensor.sensor_schema(accuracy_decimals=0, entity_category="diagnostic",
          state_class="measurement" if key == "max_tx_time" else "total_increasing",
          **({"unit_of_measurement": "µs"} if key == "max_tx_time" else {})) for key in SECPLUS2_TX_COUNTERS},
})

def _secplus1_pins(value):
    value = cv.Schema({}, extra=cv.ALLOW_EXTRA)(value).copy()
    if str(value.get("mode", "passive")).lower() == "emulate_if_needed":
        value.setdefault("tx_pin", {"number": 15, "mode": "output"})
    return value


# deferred_isr is retained for 0.4.3 entity/YAML compatibility; always zero from 0.4.4.
SECPLUS1_COUNTERS = ("rx_bytes", "frames", "door_frames", "light_lock_frames", "parity_errors",
                    "partial_timeouts", "rx_overflows", "ignored_bytes", "invalid_frames", "panel37_commands",
                    "tx_bytes", "tx_deferred", "tx_errors", "max_service_time", "rx_high_water",
                    "deferred_backlog", "deferred_partial", "deferred_byte", "deferred_isr", "deferred_high")
SECPLUS1_SCHEMA = cv.All(_secplus1_pins, cv.Schema({
    cv.Optional("mode", default="passive"): cv.one_of("passive", "emulate_if_needed", lower=True),
    cv.Optional("rx_pin", default=5): _fixed_pin(5, "input"),
    cv.Optional("tx_pin"): _fixed_pin(15, "output"),
    cv.Optional("status_timeout", default="10s"): _milliseconds(1000, 600000),
    cv.Required("status_valid"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
    cv.Required("panel_state"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Optional("last_frame"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Optional("raw_trace"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Optional("tx_block_reason"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Optional("rx_pin_high"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
    cv.Optional("light_state"): binary_sensor.binary_sensor_schema(),
    cv.Optional("lock_state"): binary_sensor.binary_sensor_schema(),
    cv.Optional("obstruction"): binary_sensor.binary_sensor_schema(device_class="problem", icon="mdi:garage-alert"),
    **{cv.Optional(key): sensor.sensor_schema(accuracy_decimals=0, entity_category="diagnostic",
          state_class="measurement" if key in ("rx_high_water", "max_service_time") else "total_increasing",
          **({"unit_of_measurement": "µs"} if key == "max_service_time" else {})) for key in SECPLUS1_COUNTERS},
}))


def _light_options(value):
    if str(value.get("restore_mode", "ALWAYS_OFF")).upper() != "ALWAYS_OFF" or value.get("effects"):
        raise cv.Invalid("Opener light requires ALWAYS_OFF bookkeeping and no effects; never restore an opener command")
    return value


SECPLUS1_CONTROL_SCHEMA = cv.Schema({
    cv.Optional("remote_lock"): lock.lock_schema(RemoteLock, icon="mdi:remote-off"),
    cv.Optional("lock_action_reason"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Optional("lock_command_count"): sensor.sensor_schema(accuracy_decimals=0, state_class="total_increasing", entity_category="diagnostic"),
    cv.Required("cover"): cover.cover_schema(Secplus1Cover, device_class="garage"),
    cv.Required("light"): cv.All(_light_options, light.light_schema(
        Secplus1Light, light.LightType.BINARY, default_restore_mode="ALWAYS_OFF")),
    cv.Required("state_valid"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
    cv.Required("firmware_update_mode"): button.button_schema(UpdateModeButton, entity_category="config"),
    cv.Optional("warning_time", default="5s"): _milliseconds(5000, 30000),
    cv.Optional("lockout_time", default="30s"): _milliseconds(5000, 120000),
    cv.Optional("buzzer_pin", default=13): _fixed_pin(13, "output"),
    cv.Required("request_toggle"): button.button_schema(ControlCommandButton),
    cv.Required("cancel"): button.button_schema(ControlCommandButton),
    cv.Required("controls_armed"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
    cv.Required("control_phase"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Required("last_action_reason"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Required("command_count"): sensor.sensor_schema(accuracy_decimals=0, state_class="total_increasing", entity_category="diagnostic"),
    cv.Required("light_action_reason"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Required("light_command_count"): sensor.sensor_schema(accuracy_decimals=0, state_class="total_increasing", entity_category="diagnostic"),
})

# Same outward warning/entity contract, with a distinct protocol-only backend.
SECPLUS2_CONTROL_SCHEMA = SECPLUS1_CONTROL_SCHEMA.extend({
    cv.Required("cover"): cover.cover_schema(Secplus2Cover, device_class="garage"),
    cv.Required("light"): cv.All(_light_options, light.light_schema(
        Secplus2Light, light.LightType.BINARY, default_restore_mode="ALWAYS_OFF")),
})

UNIFIED_CONTROL_SCHEMA = SECPLUS2_CONTROL_SCHEMA.extend({
    cv.Required("cover"): cover.cover_schema(UnifiedCover, device_class="garage"),
    cv.Required("light"): cv.All(_light_options, light.light_schema(
        UnifiedLight, light.LightType.BINARY, default_restore_mode="ALWAYS_OFF")),
    cv.Required("protocol"): select.select_schema(ProtocolSelect, entity_category="config"),
    cv.Required("panel_emulation"): select.select_schema(ProtocolSelect, entity_category="config"),
    cv.Required("configuration_state"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Optional("initial_protocol", default="unconfigured"): cv.enum({
        "unconfigured": OpenerProtocol.UNCONFIGURED, "none": OpenerProtocol.PULSE,
        "secplus1": OpenerProtocol.SECPLUS1, "secplus2": OpenerProtocol.SECPLUS2}, lower=True),
    cv.Optional("initial_panel_emulation", default="automatic"): cv.enum({
        "automatic": PanelEmulation.AUTOMATIC, "disabled": PanelEmulation.DISABLED}, lower=True),
    cv.Optional("rx_pin", default=5): _fixed_pin(5, "input"),
    cv.Optional("tx_pin", default=15): _fixed_pin(15, "output"),
    cv.Optional("pulse_time", default="1s"): _milliseconds(100, 1000),
    cv.Required("client_id"): cv.Any(cv.one_of("provisioned"), cv.int_range(min=1, max=0xFFFFFFFF)),
    cv.Optional("status_timeout", default="15s"): _milliseconds(10000, 60000),
    cv.Required("light_state"): binary_sensor.binary_sensor_schema(),
    cv.Required("obstruction"): binary_sensor.binary_sensor_schema(device_class="problem", icon="mdi:garage-alert"),
    cv.Required("panel_state"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Required("sync_state"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Required("rolling_code"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    cv.Optional("openings"): sensor.sensor_schema(accuracy_decimals=0, entity_category="diagnostic"),
})

def _threshold_number_options(value):
    if value["unit_of_measurement"] != "cm":
        raise cv.Invalid("OpenGarage thresholds use centimeters")
    if any(key in value for key in ("on_value", "on_value_range")):
        raise cv.Invalid("Threshold entities cannot attach local value automations")
    return value


THRESHOLD_NUMBER_SCHEMA = cv.All(number.number_schema(
    ThresholdNumber, entity_category="config", device_class="distance",
    unit_of_measurement="cm", icon="mdi:arrow-expand-vertical").extend({
        cv.Optional("mode", default="BOX"): cv.enum({"BOX": number.NumberMode.NUMBER_MODE_BOX}, upper=True),
    }), _threshold_number_options)

CONFIG_SCHEMA = cv.All(
    cv.only_on(["esp8266"]), _add_pins,
    cv.Schema({
        cv.GenerateID(): cv.declare_id(OpenGarageComponent),
        cv.Required("hardware"): cv.one_of("auto", "v2_0_v2_2", "v2_3", lower=True),
        cv.Optional("dev_pulse_control"): PULSE_BENCH_SCHEMA,
        cv.Optional("pulse_control"): PULSE_MVP_SCHEMA,
        cv.Optional("dev_secplus2_rx"): SECPLUS2_RX_SCHEMA,
        cv.Optional("dev_secplus2_sync"): SECPLUS2_SYNC_SCHEMA,
        cv.Optional("dev_secplus1"): SECPLUS1_SCHEMA,
        cv.Optional("secplus1_control"): SECPLUS1_CONTROL_SCHEMA,
        cv.Optional("secplus2_control"): SECPLUS2_CONTROL_SCHEMA,
        cv.Optional("unified_control"): UNIFIED_CONTROL_SCHEMA,
        cv.Optional("generic_setup"): cv.Schema({
            cv.GenerateID(): cv.declare_id(GenericSetup),
            cv.Required("api_id"): cv.use_id(api.APIServer),
            cv.Required("ota_id"): cv.use_id(ESPHomeOTAComponent),
            cv.Required("status"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
        }),
        cv.Optional("state_source", default="distance"): cv.enum(SOURCES, lower=True),
        cv.Optional("mounting", default="ceiling"): cv.enum(MOUNTINGS, lower=True),
        cv.Optional("contact_type", default="none"): cv.enum(CONTACTS, lower=True),
        cv.Optional("distance_enabled", default=True): cv.boolean,
        cv.Optional("status_led_enabled", default=False): cv.boolean,
        cv.Optional("door_threshold", default=50): cv.int_range(min=1, max=MAX_THRESHOLD_CM),
        cv.Optional("vehicle_threshold", default=150): cv.int_range(min=0, max=MAX_THRESHOLD_CM),
        cv.Optional("threshold_controls"): cv.Schema({
            cv.Required("door"): THRESHOLD_NUMBER_SCHEMA,
            cv.Required("vehicle"): THRESHOLD_NUMBER_SCHEMA,
        }),
        cv.Optional("filter", default="consensus"): cv.enum(FILTERS, lower=True),
        cv.Optional("timeout_policy", default="ignore"): cv.enum(TIMEOUTS, lower=True),
        cv.Optional("consensus_margin", default=10): cv.int_range(min=1, max=500),
        cv.Optional("sample_interval", default="500ms"): cv.All(cv.positive_time_period_milliseconds, cv.Range(min=cv.TimePeriod(milliseconds=100), max=cv.TimePeriod(seconds=5))),
        cv.Optional("stale_timeout", default="10s"): cv.All(cv.positive_time_period_milliseconds, cv.Range(max=cv.TimePeriod(seconds=60))),
        cv.Required("button_pin"): _fixed_pin(0, "input_pullup"),
        cv.Required("led_pin"): _fixed_pin(2, "output"),
        cv.Required("capability_pin"): _fixed_pin(10, "input_pullup"),
        cv.Optional("trigger_pin"): _fixed_pin(12, "output"),
        cv.Optional("echo_pin"): _fixed_pin(14, "input"),
        cv.Optional("contact_pin"): _fixed_pin(4, "input_pullup"),
        cv.Optional("distance"): sensor.sensor_schema(unit_of_measurement="cm", accuracy_decimals=0, device_class="distance", state_class="measurement"),
        cv.Optional("echo_timeouts"): sensor.sensor_schema(accuracy_decimals=0, state_class="total_increasing", entity_category="diagnostic"),
        cv.Optional("door_open"): binary_sensor.binary_sensor_schema(device_class="garage_door"),
        cv.Optional("vehicle_present"): binary_sensor.binary_sensor_schema(device_class="occupancy"),
        cv.Optional("contact_open"): binary_sensor.binary_sensor_schema(device_class="opening", entity_category="diagnostic"),
        cv.Optional("onboard_button"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
        cv.Optional("distance_fault"): binary_sensor.binary_sensor_schema(device_class="problem", entity_category="diagnostic"),
        cv.Optional("security_plus_capable"): binary_sensor.binary_sensor_schema(entity_category="diagnostic"),
        cv.Optional("door_state"): text_sensor.text_sensor_schema(),
        cv.Optional("vehicle_state"): text_sensor.text_sensor_schema(),
        cv.Optional("hardware_family"): text_sensor.text_sensor_schema(entity_category="diagnostic"),
    }).extend(cv.COMPONENT_SCHEMA), _options,
)


def _final_validate(config):
    full = fv.full_config.get()
    generic = "generic_setup" in config
    if generic:
        _validate_generic_network(full)
        if config["generic_setup"]["api_id"] != full["api"]["id"] or config["generic_setup"]["ota_id"] != next(
                item["id"] for item in full["ota"] if item["platform"] == "esphome"):
            raise cv.Invalid("Generic setup must own the configured API and OTA listeners")
    if "dev_secplus1" in config and not full.get("ota"):
        raise cv.Invalid("Security+ 1.0 status profile requires OTA lifecycle support")
    if "dev_secplus2_sync" in config:
        if not full.get("ota") or any(item["platform"] not in ("esphome", "web_server") for item in full["ota"]):
            raise cv.Invalid("Security+ 2.0 queries require audited OTA lifecycle support")
        if not full.get("api", {}).get("encryption"):
            raise cv.Invalid("Security+ 2.0 query prototype requires encrypted native API")
        if any(item["platform"] == "esphome" and not item.get("password") for item in full["ota"]):
            raise cv.Invalid("Security+ 2.0 query prototype requires authenticated native OTA")
        has_web_ota = any(item["platform"] == "web_server" for item in full["ota"])
        if (full.get("web_server") or has_web_ota) and not full.get("web_server", {}).get("auth"):
            raise cv.Invalid("Security+ 2.0 query prototype requires authenticated web_server")
    esp = full["esp8266"]
    if "unified_control" in config and not esp["restore_from_flash"]:
        raise cv.Invalid("Unified protocol selection requires restore_from_flash: true")
    if esp["board"] != "d1_mini" or esp["board_flash_mode"] != "dio" or esp["early_pin_init"]:
        raise cv.Invalid("OpenGarage M1 requires d1_mini, board_flash_mode: dio, early_pin_init: false")
    pio = full["esphome"].get("platformio_options", {})
    if pio.get("board_build.ldscript") != "eagle.flash.4m1m.ld":
        raise cv.Invalid("OpenGarage M1 requires board_build.ldscript: eagle.flash.4m1m.ld")
    if "board_build.flash_mode" in pio and pio["board_build.flash_mode"] != "dio":
        raise cv.Invalid("PlatformIO must not override DIO flash mode")
    if full.get("logger", {}).get("hardware_uart", "UART0") != "UART0":
        raise cv.Invalid("OpenGarage logger must use UART0; swapped UART0 and UART1 use reserved pins")
    if any(key in config for key in ("dev_pulse_control", "pulse_control", "secplus1_control", "secplus2_control", "unified_control")):
        if not generic and not full.get("api", {}).get("encryption"):
            raise cv.Invalid("M2 bench control requires encrypted native API")
        if not full.get("wifi") or not full.get("ota"):
            raise cv.Invalid("M2 bench control requires Wi-Fi and OTA lifecycle support")
        if any(item["platform"] not in ("esphome", "web_server") for item in full["ota"]):
            raise cv.Invalid("M2 bench control supports only audited esphome/web_server OTA platforms")
        if not generic and any(item["platform"] == "esphome" and not item.get("password") for item in full["ota"]):
            raise cv.Invalid("M2 bench control requires authenticated native OTA")
        if full.get("web_server") and not full["web_server"].get("auth"):
            raise cv.Invalid("M2 bench control requires authenticated web_server")
        if any(key in config for key in ("pulse_control", "secplus1_control", "secplus2_control", "unified_control")):
            if not full.get("web_server", {}).get("auth"):
                raise cv.Invalid("Pulse MVP requires authenticated web_server for guarded browser recovery")
            if not any(item["platform"] == "web_server" for item in full["ota"]):
                raise cv.Invalid("Pulse MVP requires web_server OTA for guarded browser recovery")
    reserved = {0, 2, 5, 10, 12, 13, 14, 15}
    if config["contact_type"] != "none":
        reserved.add(4)
    # Use the pinned ESPHome registry, including number-only schemas (e.g. UART).
    # These reservations are stronger than allow_other_uses and include unclaimed GPIO15.
    for (platform, _, number), usages in pins.PIN_SCHEMA_REGISTRY.pins_used.items():
        if platform != "esp8266" or number not in reserved:
            continue
        for path, _, _ in usages:
            if not path or path[0] != "opengarage":
                raise cv.Invalid(f"GPIO{number} is reserved by OpenGarage; conflict at {'.'.join(map(str, path))}")
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


def _validate_generic_network(full):
    # Empty fields are ONLY permitted with the runtime credential owner. Its
    # BUS+1 setup establishes auth before any network component can listen.
    if full.get("api", {}).get("encryption") != {}:
        raise cv.Invalid("Generic setup requires keyless api encryption owned by runtime setup")
    if not full.get("safe_mode", {}).get("disabled"):
        raise cv.Invalid("Generic setup currently requires safe_mode disabled: early recovery skips credential initialization")
    wifi = full.get("wifi", {})
    if full.get("web_server", {}).get("port") != 80 or wifi.get("ap", {}).get("manual_ip") or wifi.get("fast_connect", {}).get("enabled"):
        raise cv.Invalid("Generic setup requires port 80, default AP addressing and normal Wi-Fi preference layout")
    if wifi.get("networks") or wifi.get("ssid") or wifi.get("password") or wifi.get("ap", {}).get("password"):
        raise cv.Invalid("Generic setup must not contain compiled Wi-Fi credentials")
    if "ap" not in wifi or "captive_portal" not in full or not full.get("esphome", {}).get("name_add_mac_suffix"):
        raise cv.Invalid("Generic setup requires captive portal and MAC-suffixed naming")
    auth = full.get("web_server", {}).get("auth", {})
    if auth.get("type") != "digest" or auth.get("username") != "admin" or auth.get("password") != "runtime-owned-not-a-password":
        raise cv.Invalid("Generic setup requires runtime-owned admin digest authentication")
    native = [item for item in full.get("ota", []) if item["platform"] == "esphome"]
    if len(native) != 1 or native[0].get("password") != "":
        raise cv.Invalid("Generic setup requires one runtime-owned native OTA password")
    if any(key in full for key in ("mqtt", "improv_serial", "esp32_improv", "provisioning")):
        raise cv.Invalid("Generic setup owns commissioning; unaudited alternate transports are not supported")


def _mode_exposure(parent, entity, config, mask):
    """Configure exposure in generated setup(), before ANY component setup/discovery.

    ESPHome 2026.8.2's codegen-only configure_entity_ API, not the deprecated
    runtime set_internal(). Reapply the original metadata with a boot-mode bit;
    preserve an explicit internal:true. App registration itself just stores the
    pointer. Generated-source/real-core tests pin this ordering and packed layout.
    """
    fields = 0
    for key, shift in (("_entity_dc_idx", 0), ("_entity_uom_idx", 8), ("_entity_icon_idx", 16),
                       ("_entity_internal", 24), ("_entity_disabled_by_default", 25), ("_entity_category", 26)):
        fields |= config.get(key, 0) << shift
    cg.add(entity.configure_entity_(config["_entity_name"], config["_entity_object_id_hash"],
                                   parent.mode_entity_fields(fields, mask)))


async def to_code(config):
    cg.add_library("Ticker", None)  # Bundled with the pinned ESP8266 Arduino core.
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    unified = "unified_control" in config
    capability = await cg.gpio_pin_expression(config["capability_pin"])
    cg.add(var.set_capability_pin(capability))
    if config["hardware"] == "auto":
        # Before preference resolution / entity discovery, never a hot switch.
        cg.add(var.detect_unified_hardware())
    else:
        cg.add(var.set_hardware_v23(config["hardware"] == "v2_3"))
    if unified:
        dev = config["unified_control"]
        for define in ("USE_OPENGARAGE_UNIFIED", "USE_OPENGARAGE_SECPLUS1", "USE_OPENGARAGE_SECPLUS1_CONTROL",
                       "USE_OPENGARAGE_SECPLUS2_RX", "USE_OPENGARAGE_SECPLUS2_SYNC", "USE_OPENGARAGE_SECPLUS2_CONTROL"):
            cg.add_define(define)
        cg.add_library("EspSoftwareSerial", None)
        cg.add(var.load_unified_settings(dev["initial_protocol"], dev["initial_panel_emulation"]))
        cg.add(var.set_unified_pins(await cg.gpio_pin_expression(dev["rx_pin"]),
                                    await cg.gpio_pin_expression(dev["tx_pin"])))
        if dev["client_id"] != "provisioned":
            cg.add(var.set_unified_client(dev["client_id"]))
        cg.add(var.set_secplus1_timeout(dev["status_timeout"].total_milliseconds))
        cg.add(var.set_secplus2_timeout(dev["status_timeout"].total_milliseconds))
        protocol = await select.new_select(dev["protocol"], var, False,
            options=["Not configured", "None (dry contact)", "Security+ 1.0", "Security+ 2.0"])
        cg.add(var.set_protocol_select(protocol))
        _mode_exposure(var, protocol, dev["protocol"], 16)  # Hardware capability, not selected protocol.
        panel = await select.new_select(dev["panel_emulation"], var, True, options=["Automatic", "Disabled"])
        cg.add(var.set_panel_select(panel))
        _mode_exposure(var, panel, dev["panel_emulation"], 4)
        cg.add(var.set_configuration_text(await text_sensor.new_text_sensor(dev["configuration_state"])))
        for key, setter, mask in (("panel_state", "set_secplus1_panel_text", 4),
                                  ("sync_state", "set_secplus2_sync_text", 8),
                                  ("rolling_code", "set_secplus2_rolling_text", 8)):
            entity = await text_sensor.new_text_sensor(dev[key])
            cg.add(getattr(var, setter)(entity))
            _mode_exposure(var, entity, dev[key], mask)
        for key, index in (("light_state", 1), ("obstruction", 3)):
            entity = await binary_sensor.new_binary_sensor(dev[key])
            cg.add(var.set_secplus1_binary(index, entity))
            cg.add(var.set_secplus2_binary(index, entity))
            _mode_exposure(var, entity, dev[key], 12)
        if "openings" in dev:
            entity = await sensor.new_sensor(dev["openings"])
            cg.add(var.set_secplus2_openings(entity))
            _mode_exposure(var, entity, dev["openings"], 8)
    if "dev_secplus1" in config:
        dev = config["dev_secplus1"]
        cg.add_define("USE_OPENGARAGE_SECPLUS1")
        if dev["mode"] == "emulate_if_needed":
            cg.add_define("USE_OPENGARAGE_SECPLUS1_EMULATION")
            cg.add(var.set_secplus1_tx_pin(await cg.gpio_pin_expression(dev["tx_pin"])))
        cg.add_library("EspSoftwareSerial", None)
        ota.request_ota_state_listeners()
        cg.add(var.set_secplus1_rx_pin(await cg.gpio_pin_expression(dev["rx_pin"])))
        cg.add(var.set_secplus1_timeout(dev["status_timeout"].total_milliseconds))
        cg.add(var.set_secplus1_panel_text(await text_sensor.new_text_sensor(dev["panel_state"])))
        if "last_frame" in dev:
            cg.add(var.set_secplus1_frame_text(await text_sensor.new_text_sensor(dev["last_frame"])))
        if "raw_trace" in dev:
            cg.add(var.set_secplus1_trace_text(await text_sensor.new_text_sensor(dev["raw_trace"])))
        if "tx_block_reason" in dev:
            cg.add(var.set_secplus1_block_text(await text_sensor.new_text_sensor(dev["tx_block_reason"])))
        if "rx_pin_high" in dev:
            cg.add(var.set_secplus1_rx_level(await binary_sensor.new_binary_sensor(dev["rx_pin_high"])))
        for index, key in enumerate(("status_valid", "light_state", "lock_state", "obstruction")):
            if key in dev:
                cg.add(var.set_secplus1_binary(index, await binary_sensor.new_binary_sensor(dev[key])))
        for index, key in enumerate(SECPLUS1_COUNTERS):
            if key in dev:
                cg.add(var.set_secplus1_diagnostic(index, await sensor.new_sensor(dev[key])))
    if "dev_secplus2_rx" in config:
        dev = config["dev_secplus2_rx"]
        cg.add_define("USE_OPENGARAGE_SECPLUS2_RX")
        cg.add_library("EspSoftwareSerial", None)  # 8.0.1 bundled with pinned Arduino 3.1.2.
        cg.add(var.set_secplus2_rx_pin(await cg.gpio_pin_expression(dev["rx_pin"])))
        cg.add(var.set_secplus2_timeout(dev["status_timeout"].total_milliseconds))
        for index, key in enumerate(("status_valid", "light_state", "lock_state", "obstruction")):
            if key in dev:
                cg.add(var.set_secplus2_binary(index, await binary_sensor.new_binary_sensor(dev[key])))
        for index, key in enumerate(SECPLUS2_COUNTERS):
            if key in dev:
                cg.add(var.set_secplus2_diagnostic(index, await sensor.new_sensor(dev[key])))
    if "dev_secplus2_sync" in config:
        dev = config["dev_secplus2_sync"]
        cg.add_define("USE_OPENGARAGE_SECPLUS2_SYNC")
        ota.request_ota_state_listeners()
        cg.add(var.set_secplus2_query_config(await cg.gpio_pin_expression(dev["tx_pin"]), dev["client_id"]))
        cg.add(var.set_secplus2_sync_text(await text_sensor.new_text_sensor(dev["sync_state"])))
        cg.add(var.set_secplus2_rolling_text(await text_sensor.new_text_sensor(dev["rolling_code"])))
        if "openings" in dev:
            cg.add(var.set_secplus2_openings(await sensor.new_sensor(dev["openings"])))
        for index, key in enumerate(SECPLUS2_TX_COUNTERS):
            if key in dev:
                cg.add(var.set_secplus2_tx_diagnostic(index, await sensor.new_sensor(dev[key])))
    if any(key in config for key in ("dev_pulse_control", "pulse_control", "secplus1_control", "secplus2_control", "unified_control")):
        bench = "dev_pulse_control" in config
        sec1_control = "secplus1_control" in config
        sec2_control = "secplus2_control" in config
        dev = config["unified_control" if unified else "dev_pulse_control" if bench else "secplus1_control" if sec1_control else
                     "secplus2_control" if sec2_control else "pulse_control"]
        require_waveform()  # Arduino tone() must not resolve to ESPHome's no-op waveform stubs.
        cg.add_define("USE_OPENGARAGE_CONTROL")
        cg.add_define("USE_OPENGARAGE_M2_BENCH" if bench else
                      "USE_OPENGARAGE_SECPLUS1_CONTROL" if sec1_control else
                      "USE_OPENGARAGE_SECPLUS2_CONTROL" if sec2_control else
                      "USE_OPENGARAGE_UNIFIED" if unified else "USE_OPENGARAGE_PULSE_MVP")
        ota.request_ota_state_listeners()
        cg.add(var.set_bench_mode(bench))
        if bench:
            cg.add(var.set_bench_mac(str(dev["bench_mac"])))
        else:
            cg.add(var.set_cover(await cover.new_cover(dev["cover"], var)))
            cg.add(var.set_state_valid_sensor(await binary_sensor.new_binary_sensor(dev["state_valid"])))
            await button.new_button(dev["firmware_update_mode"], var)
        if sec1_control or sec2_control or unified:
            door = await cg.get_variable(dev["tx_pin"][CONF_ID]) if unified else cg.nullptr
            cg.add(var.set_control_pins(door, await cg.gpio_pin_expression(dev["buzzer_pin"])))
            pulse_ms = dev["pulse_time"].total_milliseconds if unified else 1000
            cg.add(var.set_control_timing(dev["warning_time"].total_milliseconds, pulse_ms,
                                         dev["lockout_time"].total_milliseconds))
            output = cg.new_Pvariable(dev["light"]["output_id"], var)
            await light.register_light(output, dev["light"])
            cg.add(var.set_light(output))
            cg.add(var.set_light_reason(await text_sensor.new_text_sensor(dev["light_action_reason"])))
            cg.add(var.set_light_count(await sensor.new_sensor(dev["light_command_count"])))
            if "remote_lock" in dev:
                cg.add(var.set_remote_lock(await lock.new_lock(dev["remote_lock"], var)))
            if "lock_action_reason" in dev:
                cg.add(var.set_lock_reason(await text_sensor.new_text_sensor(dev["lock_action_reason"])))
            if "lock_command_count" in dev:
                cg.add(var.set_lock_count(await sensor.new_sensor(dev["lock_command_count"])))
            cg.add(var.set_pulse_count_sensor(await sensor.new_sensor(dev["command_count"])))
            if unified:
                for key in ("light", "remote_lock", "light_action_reason", "light_command_count",
                            "lock_action_reason", "lock_command_count"):
                    if key in dev:
                        _mode_exposure(var, await cg.get_variable(dev[key][CONF_ID]), dev[key], 12)
        else:
            cg.add(var.set_control_pins(await cg.gpio_pin_expression(dev["door_pin"]),
                                        await cg.gpio_pin_expression(dev["buzzer_pin"])))
            cg.add(var.set_control_timing(*(dev[key].total_milliseconds for key in
                                          ("warning_time", "pulse_time", "lockout_time"))))
        for key, cancel in (("request_toggle", False), ("cancel", True)):
            await button.new_button(dev[key], var, cancel)
        if "controls_armed" in dev:
            cg.add(var.set_armed_sensor(await binary_sensor.new_binary_sensor(dev["controls_armed"])))
        for key, setter in (("control_phase", "set_phase_text"), ("last_action_reason", "set_reason_text")):
            if key in dev:
                cg.add(getattr(var, setter)(await text_sensor.new_text_sensor(dev[key])))
        if "pulse_count" in dev:
            cg.add(var.set_pulse_count_sensor(await sensor.new_sensor(dev["pulse_count"])))
    cg.add(var.set_status_led_enabled(config["status_led_enabled"]))
    for key, setter in (("state_source", "set_source"), ("mounting", "set_mounting"),
                        ("contact_type", "set_contact_type"), ("distance_enabled", "set_distance_enabled"),
                        ("door_threshold", "set_door_threshold"), ("vehicle_threshold", "set_vehicle_threshold")):
        cg.add(getattr(var, setter)(config[key]))
    if "threshold_controls" in config:
        cg.add_define("USE_OPENGARAGE_THRESHOLDS")
        # Load after YAML defaults AND the existing unified-mode preference;
        # before App.setup() and any sensor/control publication.
        cg.add(var.load_threshold_settings())
        for key, door in (("door", True), ("vehicle", False)):
            conf = config["threshold_controls"][key]
            entity = await number.new_number(conf, var, door, min_value=1 if door else 0, max_value=MAX_THRESHOLD_CM, step=1)
            cg.add(var.set_threshold_number(door, entity))
            if unified and door:
                _mode_exposure(var, entity, conf, 2)  # None/pulse only; preserve static internal flags.
    for key in ("button_pin", "led_pin", "contact_pin"):
        if key in config:
            cg.add(getattr(var, "set_" + key)(await cg.gpio_pin_expression(config[key])))
    if config["distance_enabled"]:
        trigger = await cg.gpio_pin_expression(config["trigger_pin"])
        echo = await cg.gpio_pin_expression(config["echo_pin"])
        cg.add(var.set_distance_pins(trigger, echo))
        cg.add(var.set_distance_config(config["filter"], config["timeout_policy"], config["consensus_margin"],
                                       config["stale_timeout"].total_milliseconds, config["sample_interval"].total_milliseconds))
    for key, setter in (("distance", "set_distance_sensor"), ("echo_timeouts", "set_timeout_sensor")):
        if key in config:
            cg.add(getattr(var, setter)(await sensor.new_sensor(config[key])))
    for key, setter in (("door_open", "set_door_sensor"), ("vehicle_present", "set_vehicle_sensor"),
                        ("contact_open", "set_contact_sensor"), ("onboard_button", "set_button_sensor"),
                        ("distance_fault", "set_health_sensor"), ("security_plus_capable", "set_capability_sensor")):
        if key in config:
            cg.add(getattr(var, setter)(await binary_sensor.new_binary_sensor(config[key])))
    for key, setter in (("door_state", "set_door_text"), ("vehicle_state", "set_vehicle_text"), ("hardware_family", "set_family_text")):
        if key in config:
            cg.add(getattr(var, setter)(await text_sensor.new_text_sensor(config[key])))
    if "generic_setup" in config:
        conf = config["generic_setup"]
        cg.add_define("USE_OPENGARAGE_GENERIC_SETUP")
        # The pinned API guard also means 'externally owned PSK': no upstream
        # preference may overwrite it, and no API client may clear/replace it.
        # There is deliberately NO YAML PSK or generated set_noise_psk literal.
        cg.add_define("USE_API_NOISE_PSK_FROM_YAML")
        setup = cg.new_Pvariable(conf[CONF_ID], var,
                                await cg.get_variable(conf["api_id"]),
                                await cg.get_variable(conf["ota_id"]))
        await cg.register_component(setup, conf)
        cg.add(setup.set_status_sensor(await text_sensor.new_text_sensor(conf["status"])))
        # Keep the original mode and threshold records first. All later generic
        # versions must retain this allocation order and fixed record size.
        cg.add(setup.load_credentials())
