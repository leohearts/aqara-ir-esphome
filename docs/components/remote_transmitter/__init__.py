# Replaces the stock remote_transmitter: instead of driving a GPIO, the encoded
# mark/space timings are written to stdout for the Aqara mha_ir plugin.
import esphome.codegen as cg
from esphome.components import remote_base
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["remote_base"]

remote_transmitter_ns = cg.esphome_ns.namespace("remote_transmitter")
RemoteTransmitterComponent = remote_transmitter_ns.class_(
    "RemoteTransmitterComponent", remote_base.RemoteTransmitterBase, cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(): cv.declare_id(RemoteTransmitterComponent)}
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
