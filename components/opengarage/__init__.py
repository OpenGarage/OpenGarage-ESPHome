# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only by default; separate bench and dry-contact pulse MVP opt-ins."""
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import pins
from esphome.components import binary_sensor, sensor, text_sensor, button, cover, ota
from esphome.components.esp8266.const import require_waveform
from esphome.const import CONF_ID

def AUTO_LOAD(config):
    components = ["sensor", "binary_sensor", "text_sensor"]
    if "dev_pulse_control" in config or "pulse_control" in config:
        components.append("button")
    if "pulse_control" in config:
        components += ["cover", "web_server_base"]
    return components
DEPENDENCIES = ["esp8266"]
MULTI_CONF = False

ns = cg.esphome_ns.namespace("opengarage")
OpenGarageComponent = ns.class_("OpenGarageComponent", cg.Component)
ControlCommandButton = ns.class_("ControlCommandButton", button.Button)
PulseCover = ns.class_("PulseCover", cover.Cover)
UpdateModeButton = ns.class_("UpdateModeButton", button.Button)
StateSource = ns.enum("StateSource", is_class=True)
Mounting = ns.enum("Mounting", is_class=True)
ContactType = ns.enum("ContactType", is_class=True)
FilterMode = ns.enum("FilterMode", is_class=True)
TimeoutPolicy = ns.enum("TimeoutPolicy", is_class=True)
SOURCES = {name.lower(): getattr(StateSource, name) for name in ("DISTANCE", "CONTACT", "BOTH_AND", "EITHER_OR")}
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
    if source != "contact" and not value["distance_enabled"]:
        raise cv.Invalid("This state_source requires distance_enabled: true")
    if source != "distance" and value["contact_type"] == "none":
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


CONFIG_SCHEMA = cv.All(
    cv.only_on(["esp8266"]), _add_pins,
    cv.Schema({
        cv.GenerateID(): cv.declare_id(OpenGarageComponent),
        cv.Required("hardware"): cv.one_of("v2_0_v2_2", "v2_3", lower=True),
        cv.Optional("dev_pulse_control"): PULSE_BENCH_SCHEMA,
        cv.Optional("pulse_control"): PULSE_MVP_SCHEMA,
        cv.Optional("state_source", default="distance"): cv.enum(SOURCES, lower=True),
        cv.Optional("mounting", default="ceiling"): cv.enum(MOUNTINGS, lower=True),
        cv.Optional("contact_type", default="none"): cv.enum(CONTACTS, lower=True),
        cv.Optional("distance_enabled", default=True): cv.boolean,
        cv.Optional("status_led_enabled", default=False): cv.boolean,
        cv.Optional("door_threshold", default=50): cv.int_range(min=1, max=500),
        cv.Optional("vehicle_threshold", default=150): cv.int_range(min=0, max=500),
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
    esp = full["esp8266"]
    if esp["board"] != "d1_mini" or esp["board_flash_mode"] != "dio" or esp["early_pin_init"]:
        raise cv.Invalid("OpenGarage M1 requires d1_mini, board_flash_mode: dio, early_pin_init: false")
    pio = full["esphome"].get("platformio_options", {})
    if pio.get("board_build.ldscript") != "eagle.flash.4m1m.ld":
        raise cv.Invalid("OpenGarage M1 requires board_build.ldscript: eagle.flash.4m1m.ld")
    if "board_build.flash_mode" in pio and pio["board_build.flash_mode"] != "dio":
        raise cv.Invalid("PlatformIO must not override DIO flash mode")
    if full.get("logger", {}).get("hardware_uart", "UART0") != "UART0":
        raise cv.Invalid("OpenGarage logger must use UART0; swapped UART0 and UART1 use reserved pins")
    if "dev_pulse_control" in config or "pulse_control" in config:
        if not full.get("api", {}).get("encryption"):
            raise cv.Invalid("M2 bench control requires encrypted native API")
        if not full.get("wifi") or not full.get("ota"):
            raise cv.Invalid("M2 bench control requires Wi-Fi and OTA lifecycle support")
        if any(item["platform"] not in ("esphome", "web_server") for item in full["ota"]):
            raise cv.Invalid("M2 bench control supports only audited esphome/web_server OTA platforms")
        if any(item["platform"] == "esphome" and not item.get("password") for item in full["ota"]):
            raise cv.Invalid("M2 bench control requires authenticated native OTA")
        if full.get("web_server") and not full["web_server"].get("auth"):
            raise cv.Invalid("M2 bench control requires authenticated web_server")
        if "pulse_control" in config:
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


async def to_code(config):
    cg.add_library("Ticker", None)  # Bundled with the pinned ESP8266 Arduino core.
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if "dev_pulse_control" in config or "pulse_control" in config:
        bench = "dev_pulse_control" in config
        dev = config["dev_pulse_control"] if bench else config["pulse_control"]
        require_waveform()  # Arduino tone() must not resolve to ESPHome's no-op waveform stubs.
        cg.add_define("USE_OPENGARAGE_CONTROL")
        cg.add_define("USE_OPENGARAGE_M2_BENCH" if bench else "USE_OPENGARAGE_PULSE_MVP")
        ota.request_ota_state_listeners()
        cg.add(var.set_bench_mode(bench))
        if bench:
            cg.add(var.set_bench_mac(str(dev["bench_mac"])))
        else:
            cg.add(var.set_cover(await cover.new_cover(dev["cover"], var)))
            cg.add(var.set_state_valid_sensor(await binary_sensor.new_binary_sensor(dev["state_valid"])))
            await button.new_button(dev["firmware_update_mode"], var)
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
    cg.add(var.set_hardware_v23(config["hardware"] == "v2_3"))
    cg.add(var.set_status_led_enabled(config["status_led_enabled"]))
    for key, setter in (("state_source", "set_source"), ("mounting", "set_mounting"),
                        ("contact_type", "set_contact_type"), ("distance_enabled", "set_distance_enabled"),
                        ("door_threshold", "set_door_threshold"), ("vehicle_threshold", "set_vehicle_threshold")):
        cg.add(getattr(var, setter)(config[key]))
    for key in ("button_pin", "led_pin", "capability_pin", "contact_pin"):
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
