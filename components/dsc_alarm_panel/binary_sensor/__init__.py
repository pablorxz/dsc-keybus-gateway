"""DSC zone, connection, trouble, and fire binary entities."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_NAME
from .. import DscAlarmPanel, namespace

DEPENDENCIES = ["dsc_alarm_panel"]
CONF_PANEL_ID = "panel_id"
CONF_KIND = "kind"
CONF_ZONE = "zone"
CONF_GROUP = "group"
CONF_CATEGORY = "category"
CONF_DISPLAY_NAME = "display_name"
KINDS = {"connection": 0, "trouble": 1, "fire": 2, "zone": 3}
DscBinarySensor = namespace.class_("DscBinarySensor", binary_sensor.BinarySensor, cg.Component)

def validate_zone(config):
    if config[CONF_KIND] == "zone" and not config.get(CONF_ZONE):
        raise cv.Invalid("A zone entity requires zone: 1..64")
    if config[CONF_KIND] != "zone" and CONF_ZONE in config:
        raise cv.Invalid("zone is only valid for a zone entity")
    if config[CONF_KIND] == "zone" and not config.get(CONF_GROUP):
        raise cv.Invalid("A zone entity requires a display group")
    return config

CONFIG_SCHEMA = cv.All(binary_sensor.binary_sensor_schema(DscBinarySensor).extend({
    cv.Required(CONF_PANEL_ID): cv.use_id(DscAlarmPanel),
    cv.Required(CONF_KIND): cv.one_of(*KINDS, lower=True),
    cv.Optional(CONF_ZONE): cv.int_range(min=1, max=64),
    cv.Optional(CONF_GROUP): cv.string_strict,
    cv.Optional(CONF_CATEGORY, default="sensor"): cv.one_of("sensor", "motion", "door", "window", lower=True),
    cv.Optional(CONF_DISPLAY_NAME): cv.string_strict,
}).extend(cv.COMPONENT_SCHEMA), validate_zone)

async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_PANEL_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_kind(KINDS[config[CONF_KIND]]))
    if CONF_ZONE in config:
        cg.add(var.set_zone(config[CONF_ZONE]))
        cg.add(parent.register_zone(config[CONF_ZONE], config.get(CONF_DISPLAY_NAME, config[CONF_NAME]), config[CONF_GROUP], config[CONF_CATEGORY]))
