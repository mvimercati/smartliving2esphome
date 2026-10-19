"""INIM SmartLiving 1050 on the ESP UART.

The installation (expansions, terminals, areas, scenarios) is described in installation.yaml
and every entity is generated from it. Renaming a zone or an area here creates a new entity in
Home Assistant (the unique id comes from the name): rename them in Home Assistant instead.
"""

import re

import esphome.codegen as cg
from esphome.components import binary_sensor, button, sensor, text_sensor, uart
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_TIMEOUT,
    CONF_TYPE,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_TAMPER,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_VOLT,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "button", "sensor", "text_sensor"]

inim_ns = cg.esphome_ns.namespace("inim")
InimPanel = inim_ns.class_("InimPanel", cg.Component, uart.UARTDevice)
RefreshButton = inim_ns.class_("RefreshButton", button.Button)

CONF_EXPANSIONS = "expansions"
CONF_TERMINALS = "terminals"
CONF_AREAS = "areas"
CONF_SCENARIOS = "scenarios"
CONF_ZONE = "zone"
CONF_SETTLE = "settle"
CONF_POLL_INTERVAL = "poll_interval"
CONF_SLOW_POLL_INTERVAL = "slow_poll_interval"
CONF_OFFLINE_AFTER = "offline_after"
CONF_COMMAND_MAX_AGE = "command_max_age"
CONF_CONFIRM_READS = "confirm_reads"
CONF_WRITE_ATTEMPTS = "write_attempts"
CONF_LOG_MAX_ENTRIES = "log_max_entries_per_poll"
CONF_PSU_VOLTAGE_THRESHOLD = "psu_voltage_threshold"
_ENTITIES = "_entities"  # generated entity configs

NUM_ZONES = 50  # SmartLiving 1050
NUM_AREAS = 10
EXPANSION_TERMINALS = 5
SCENARIOS = ["disarmed", "armed_away", "armed_home", "armed_night"]  # order of the C++ Scenario enum
# Binary sensor device classes accepted as zone "type"
ZONE_TYPES = ["window", "door", "garage_door", "opening", "motion", "occupancy", "presence", "vibration",
              "smoke", "gas", "moisture", "safety", "tamper", "problem"]

# One binary sensor per known bit of the faults block, in the order of FAULT_BITS (inim.h)
FAULTS = [
    ("Mancanza rete", DEVICE_CLASS_PROBLEM),  # fault_mains
    ("Batteria inefficiente", DEVICE_CLASS_PROBLEM),  # fault_battery
    ("Fusibile zone", DEVICE_CLASS_PROBLEM),  # fuse_zones
    ("Fusibile I-BUS", DEVICE_CLASS_PROBLEM),  # fuse_ibus
    ("Linea telefonica", DEVICE_CLASS_PROBLEM),  # fault_phone
    ("Modulo GSM", DEVICE_CLASS_PROBLEM),  # fault_gsm
    ("Jamming radio", DEVICE_CLASS_PROBLEM),  # fault_jamming
    ("Batteria sensori radio", DEVICE_CLASS_PROBLEM),  # fault_radio_battery
    ("Perdita sensori radio", DEVICE_CLASS_PROBLEM),  # fault_radio_loss
    ("Sabotaggio centrale", DEVICE_CLASS_TAMPER),  # tamper_panel
    ("Coperchio centrale", DEVICE_CLASS_TAMPER),  # tamper_cover
    ("Sabotaggio lettori", DEVICE_CLASS_TAMPER),  # tamper_reader
    ("Sabotaggio tastiere", DEVICE_CLASS_TAMPER),  # tamper_keypad
    ("Sabotaggio espansioni", DEVICE_CLASS_TAMPER),  # tamper_expander
    ("Reset", None),
    ("Internet", None),
    ("Manutenzione", None),  # service
    ("Programmazione", None),  # program
    ("Vocale", None),  # voice
]

# Summary binary sensors, in the order of the C++ Summary enum
SUMMARIES = [
    ("Sabotaggio", DEVICE_CLASS_TAMPER),
    ("Memoria allarme", DEVICE_CLASS_PROBLEM),
    ("Memoria sabotaggio", DEVICE_CLASS_PROBLEM),
    ("Guasto", DEVICE_CLASS_PROBLEM),
    ("Guasto alimentazione", DEVICE_CLASS_PROBLEM),
    ("Guasto comunicazioni", DEVICE_CLASS_PROBLEM),
    ("Guasto radio", DEVICE_CLASS_PROBLEM),
]

# Text sensors: setter -> (name, icon, diagnostic)
TEXT_SENSORS = {
    "set_alarm_state_sensor": ("Stato antifurto", "mdi:shield", True),  # HA state for the template panel
    "set_zone_alarm_sensor": ("Zone in allarme", "mdi:alarm-light", False),
    "set_last_event_sensor": ("Ultimo evento", "mdi:format-list-bulleted", False),
    "set_last_event_time_sensor": ("Ora ultimo evento", "mdi:clock-outline", True),  # panel clock
    "set_faults_summary_sensor": ("Faults summary", "mdi:alert-circle-outline", True),
    "set_scenario_code_sensor": ("Codice scenario", "mdi:numeric", True),
    "set_last_write_sensor": ("Last write result", "mdi:message-reply-text-outline", True),
}

_TERMINAL_RE = re.compile(r"^(?:T(\d+)|E(\d+)\.T(\d+)|K(\d+)\.T(\d+))$", re.IGNORECASE)


def terminal_index(label: str, expansions: int) -> int:
    """Physical terminal number used by the panel (numbering from inim.java):
    T1-T10 on the panel -> 0-9, E<e>.T<t> on expansion e -> 10 + 5(e-1) + (t-1),
    K<k>.T<t> on keypad k -> 110 + 2(k-1) + (t-1)."""
    m = _TERMINAL_RE.match(label.strip())
    if not m:
        raise cv.Invalid(f"terminal '{label}': expected T<n>, E<n>.T<n> or K<n>.T<n>")
    t, exp, exp_t, kpd, kpd_t = (int(g) if g else None for g in m.groups())
    if t is not None:
        if not 1 <= t <= 10:
            raise cv.Invalid(f"terminal '{label}': panel terminals are T1-T10")
        return t - 1
    if exp is not None:
        if not 1 <= exp <= expansions:
            raise cv.Invalid(f"terminal '{label}': expansion E{exp} not installed (expansions: {expansions})")
        if not 1 <= exp_t <= EXPANSION_TERMINALS:
            raise cv.Invalid(f"terminal '{label}': expansion terminals are T1-T{EXPANSION_TERMINALS}")
        return 10 + (exp - 1) * EXPANSION_TERMINALS + exp_t - 1
    if not 1 <= kpd <= 10 or not 1 <= kpd_t <= 2:
        raise cv.Invalid(f"terminal '{label}': keypads are K1-K10, terminals T1-T2")
    return 110 + (kpd - 1) * 2 + kpd_t - 1


TERMINAL_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): cv.string_strict,
        cv.Optional(CONF_TYPE): cv.one_of(*ZONE_TYPES, lower=True),
        cv.Optional(CONF_ZONE): cv.int_range(min=0, max=NUM_ZONES - 1),  # if it differs from the terminal
    }
)


def _terminal(value):
    return None if value is None else TERMINAL_SCHEMA(value)  # listed but unused


def _scenario_code(value):
    value = cv.string_strict(value)
    if not re.fullmatch(r"[0-9A-Fa-f]{12}", value):
        raise cv.Invalid("a scenario is 12 hex digits (the 'Codice scenario' sensor)")
    return value.upper()


def _binary(name, device_class=None, icon=None, diagnostic=False):
    kwargs = {}
    if device_class:
        kwargs["device_class"] = device_class
    if icon:
        kwargs["icon"] = icon
    if diagnostic:
        kwargs["entity_category"] = ENTITY_CATEGORY_DIAGNOSTIC
    return binary_sensor.binary_sensor_schema(**kwargs)({CONF_NAME: name})


def _text(name, icon, diagnostic=False):
    kwargs = {"icon": icon}
    if diagnostic:
        kwargs["entity_category"] = ENTITY_CATEGORY_DIAGNOSTIC
    return text_sensor.text_sensor_schema(**kwargs)({CONF_NAME: name})


def _voltage(name):
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
    )({CONF_NAME: name})


def _build_entities(config):
    """Expand the installation into entity configs (validated like any other entity)."""
    zones = []
    for label, item in config[CONF_TERMINALS].items():
        if item is None:
            continue
        terminal = terminal_index(str(label), config[CONF_EXPANSIONS])
        zone = item.get(CONF_ZONE, terminal)
        if zone >= NUM_ZONES:
            raise cv.Invalid(f"terminal {label}: zone number {zone} out of range, set 'zone' (0-{NUM_ZONES - 1})")
        if zone in (z[CONF_ZONE] for z in zones):
            raise cv.Invalid(f"terminal {label}: zone {zone} used twice")
        name = item[CONF_NAME]
        zones.append({
            CONF_ZONE: zone,
            "terminal": terminal,
            CONF_NAME: name,
            "state": _binary(name, item.get(CONF_TYPE)),
            # Numbered, not named: the Home Assistant locks find it by zone number
            "bypass": _binary(f"Esclusione zona {zone}", icon="mdi:shield-off-outline", diagnostic=True),
        })
    areas = [
        {
            CONF_ID: area,
            CONF_NAME: name,
            # HA alarm state for the template panel, which finds it by area number
            "state": _text(f"Stato area {area}", "mdi:shield", diagnostic=True),
        }
        for area, name in config[CONF_AREAS].items()
    ]
    config[_ENTITIES] = {
        "zones": zones,
        "areas": areas,
        "faults": [_binary(name, dc, diagnostic=True) for name, dc in FAULTS],
        "summaries": [_binary(name, dc) for name, dc in SUMMARIES],
        "connection": _binary("Connessione centrale", DEVICE_CLASS_CONNECTIVITY, diagnostic=True),
        "bypassed": _binary("Zone escluse", icon="mdi:shield-off"),
        "battery": _voltage("Battery voltage"),
        "psu": _voltage("PSU voltage"),
        "texts": {setter: _text(name, icon, diag) for setter, (name, icon, diag) in TEXT_SENSORS.items()},
        "refresh": button.button_schema(
            RefreshButton, entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:refresh"
        )({CONF_NAME: "Refresh"}),
    }
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(InimPanel),
            cv.Optional(CONF_EXPANSIONS, default=0): cv.int_range(min=0, max=20),
            cv.Required(CONF_TERMINALS): cv.Schema({cv.string: _terminal}),
            cv.Required(CONF_AREAS): cv.Schema({cv.int_range(min=0, max=NUM_AREAS - 1): cv.string_strict}),
            cv.Optional(CONF_SCENARIOS, default={}): cv.Schema({cv.one_of(*SCENARIOS): _scenario_code}),
            cv.Optional(CONF_TIMEOUT, default="1s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_SETTLE, default="500ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_POLL_INTERVAL, default="1s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_SLOW_POLL_INTERVAL, default="5s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_OFFLINE_AFTER, default="30s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_COMMAND_MAX_AGE, default="20s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_CONFIRM_READS, default=3): cv.int_range(min=0, max=20),
            cv.Optional(CONF_WRITE_ATTEMPTS, default=2): cv.int_range(min=1, max=5),
            cv.Optional(CONF_LOG_MAX_ENTRIES, default=20): cv.int_range(min=1, max=100),
            cv.Optional(CONF_PSU_VOLTAGE_THRESHOLD, default=13.0): cv.float_,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA),
    _build_entities,
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "inim", baud_rate=57600, require_tx=True, require_rx=True, parity="EVEN", data_bits=8, stop_bits=1
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_timeout(config[CONF_TIMEOUT]))
    cg.add(var.set_settle(config[CONF_SETTLE]))
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_slow_poll_interval(config[CONF_SLOW_POLL_INTERVAL]))
    cg.add(var.set_offline_after(config[CONF_OFFLINE_AFTER]))
    cg.add(var.set_command_max_age(config[CONF_COMMAND_MAX_AGE]))
    cg.add(var.set_confirm_reads(config[CONF_CONFIRM_READS]))
    cg.add(var.set_write_attempts(config[CONF_WRITE_ATTEMPTS]))
    cg.add(var.set_log_max_entries(config[CONF_LOG_MAX_ENTRIES]))
    cg.add(var.set_psu_voltage_threshold(config[CONF_PSU_VOLTAGE_THRESHOLD]))
    for scenario, code in config[CONF_SCENARIOS].items():
        cg.add(var.set_scenario(SCENARIOS.index(scenario), code))

    entities = config[_ENTITIES]
    for z in entities["zones"]:
        state = await binary_sensor.new_binary_sensor(z["state"])
        bypass = await binary_sensor.new_binary_sensor(z["bypass"])
        cg.add(var.add_zone(z[CONF_ZONE], z["terminal"], z[CONF_NAME], state, bypass))
    for a in entities["areas"]:
        state = await text_sensor.new_text_sensor(a["state"])
        cg.add(var.add_area(a[CONF_ID], a[CONF_NAME], state))
    for i, conf in enumerate(entities["faults"]):
        cg.add(var.set_fault_sensor(i, await binary_sensor.new_binary_sensor(conf)))
    for i, conf in enumerate(entities["summaries"]):
        cg.add(var.set_summary_sensor(i, await binary_sensor.new_binary_sensor(conf)))
    cg.add(var.set_connection_sensor(await binary_sensor.new_binary_sensor(entities["connection"])))
    cg.add(var.set_bypassed_sensor(await binary_sensor.new_binary_sensor(entities["bypassed"])))
    cg.add(var.set_battery_sensor(await sensor.new_sensor(entities["battery"])))
    cg.add(var.set_psu_sensor(await sensor.new_sensor(entities["psu"])))
    for setter, conf in entities["texts"].items():
        cg.add(getattr(var, setter)(await text_sensor.new_text_sensor(conf)))
    refresh = await button.new_button(entities["refresh"])
    await cg.register_parented(refresh, var)
