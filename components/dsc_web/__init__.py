"""Authenticated local dashboard over the shared DSC application service."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import web_server_base
from esphome.const import CONF_ID, CONF_PASSWORD, CONF_PORT, CONF_USERNAME
from esphome.components.dsc_alarm_panel import DscAlarmPanel

DEPENDENCIES = ["dsc_alarm_panel", "network"]
AUTO_LOAD = ["web_server_base", "json"]
CONF_PANEL_ID = "panel_id"
CONF_AUTH = "auth"
namespace = cg.esphome_ns.namespace("dsc_web")
DscWeb = namespace.class_("DscWeb", cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(DscWeb),
    cv.Required(CONF_PANEL_ID): cv.use_id(DscAlarmPanel),
    cv.GenerateID(web_server_base.CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
    cv.Optional(CONF_PORT, default=80): cv.port,
    cv.Required(CONF_AUTH): cv.Schema({
        cv.Required(CONF_USERNAME): cv.All(cv.string_strict, cv.Length(min=1)),
        cv.Required(CONF_PASSWORD): cv.sensitive(cv.All(cv.string_strict, cv.Length(min=12))),
    }),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    cg.add_define("USE_WEBSERVER_AUTH")
    cg.add_define("USE_WEBSERVER_AUTH_DIGEST")
    base = await cg.get_variable(config[web_server_base.CONF_WEB_SERVER_BASE_ID])
    cg.add(base.set_port(config[CONF_PORT]))
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_parent(await cg.get_variable(config[CONF_PANEL_ID])))
    cg.add(var.set_base(base))
    cg.add(var.set_auth(config[CONF_AUTH][CONF_USERNAME], config[CONF_AUTH][CONF_PASSWORD]))
