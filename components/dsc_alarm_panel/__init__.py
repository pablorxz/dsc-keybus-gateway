"""Thin ESPHome integration; core and application are linked local libraries."""
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]
namespace = cg.esphome_ns.namespace("dsc_alarm_panel")
DscAlarmPanel = namespace.class_("DscAlarmPanel", cg.Component)
CONF_SIMULATION = "simulation"
CONF_PHYSICAL_BUS = "physical_bus"
CONF_CLOCK_PIN = "clock_pin"
CONF_READ_PIN = "read_pin"
CONF_WRITE_PIN = "write_pin"
VALID_INPUTS = set(range(0, 20)) | {21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 37, 38, 39}
VALID_INPUTS -= set(range(6, 12))
VALID_OUTPUTS = VALID_INPUTS - {34, 35, 36, 37, 38, 39}
def validate_transport(config):
    if config[CONF_SIMULATION] and config[CONF_PHYSICAL_BUS]:
        raise cv.Invalid("simulation and physical_bus cannot both be enabled")
    if config[CONF_PHYSICAL_BUS]:
        if CONF_CLOCK_PIN not in config or CONF_READ_PIN not in config:
            raise cv.Invalid("physical_bus requires explicit clock_pin and read_pin")
        clock, read = config[CONF_CLOCK_PIN], config[CONF_READ_PIN]
        write = config.get(CONF_WRITE_PIN, 255)
        if clock not in VALID_INPUTS or read not in VALID_INPUTS or (write != 255 and write not in VALID_OUTPUTS):
            raise cv.Invalid("invalid ESP32 GPIO for Keybus input or output")
        if clock == read or clock == write:
            raise cv.Invalid("clock_pin must differ from data pins")
    return config

CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(DscAlarmPanel),
        cv.Optional(CONF_SIMULATION, default=False): cv.boolean,
        cv.Optional(CONF_PHYSICAL_BUS, default=False): cv.boolean,
        cv.Optional(CONF_CLOCK_PIN): cv.int_range(min=0, max=39),
        cv.Optional(CONF_READ_PIN): cv.int_range(min=0, max=39),
        cv.Optional(CONF_WRITE_PIN): cv.int_range(min=0, max=39),
    }).extend(cv.COMPONENT_SCHEMA),
    validate_transport,
)


async def to_code(config):
    root = Path(__file__).resolve().parents[2]
    for name in ("dsc_core", "dsc_app"):
        folder = root / "lib" / name
        if not (folder / "library.json").is_file():
            raise cv.Invalid(f"Missing shared library: {folder}")
        # ESPHome 2026.10's supported LocalSource compiles these sources in place,
        # including nested platform files. There is no generated source copy.
        cg.add_library(name.replace("_", "-"), None, folder.as_uri())
    if config[CONF_SIMULATION]:
        cg.add_build_flag("-DDSC_ENABLE_REPLAY")
        cg.add_library("dsc-bench-model", None, (root / "firmware" / "simulator").as_uri())
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_simulation(config[CONF_SIMULATION]))
    cg.add(var.set_physical_bus(config[CONF_PHYSICAL_BUS]))
    if config[CONF_PHYSICAL_BUS]:
        cg.add(var.set_pins(config[CONF_CLOCK_PIN], config[CONF_READ_PIN], config.get(CONF_WRITE_PIN, 255)))
