"""Native ESPHome alarm entity backed by the shared alarm service."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import alarm_control_panel
from esphome.const import CONF_ID
from .. import DscAlarmPanel, namespace

DEPENDENCIES = ["dsc_alarm_panel"]
CONF_PANEL_ID = "panel_id"
CONF_PARTITION = "partition"
DscPartition = namespace.class_("DscPartition", alarm_control_panel.AlarmControlPanel, cg.Component)
CONFIG_SCHEMA = alarm_control_panel.alarm_control_panel_schema(DscPartition).extend({
    cv.Required(CONF_PANEL_ID): cv.use_id(DscAlarmPanel),
    cv.Optional(CONF_PARTITION, default=1): cv.int_range(min=1, max=8),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = await alarm_control_panel.new_alarm_control_panel(config)
    await cg.register_component(var, config)
    cg.add(var.set_parent(await cg.get_variable(config[CONF_PANEL_ID])))
    cg.add(var.set_partition(config[CONF_PARTITION]))
