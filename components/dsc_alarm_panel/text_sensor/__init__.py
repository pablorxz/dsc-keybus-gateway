"""Latest shared operation result for Home Assistant."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from .. import DscAlarmPanel, namespace

DEPENDENCIES = ["dsc_alarm_panel"]
DscOperationSensor = namespace.class_("DscOperationSensor", text_sensor.TextSensor, cg.Component)
CONFIG_SCHEMA = text_sensor.text_sensor_schema(DscOperationSensor).extend({
    cv.Required("panel_id"): cv.use_id(DscAlarmPanel),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config["panel_id"])
    cg.add(var.set_parent(parent))
