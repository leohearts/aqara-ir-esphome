# Replaces the stock remote_transmitter: instead of driving a GPIO, the encoded
# mark/space timings are written to stdout for the Aqara mha_ir plugin.
# repeat: how many copies of each frame to send (libirplug.so takes at most 998 timings).
import esphome.codegen as cg
from esphome.components import remote_base
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["remote_base"]

remote_transmitter_ns = cg.esphome_ns.namespace("remote_transmitter")
RemoteTransmitterComponent = remote_transmitter_ns.class_(
    "RemoteTransmitterComponent", remote_base.RemoteTransmitterBase, cg.Component
)

CONF_REPEAT = "repeat"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RemoteTransmitterComponent),
        cv.Optional(CONF_REPEAT, default=1): cv.int_range(min=1, max=10),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_repeat(config[CONF_REPEAT]))
