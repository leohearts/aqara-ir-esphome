# Replaces the stock remote_receiver: instead of reading a GPIO, aqara_ir injects the timings
# that the P3's IR module received ($AQARA_RX), and the climate_ir listeners decode them.
import esphome.codegen as cg
from esphome.components import remote_base
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_IDLE

AUTO_LOAD = ["remote_base"]

remote_receiver_ns = cg.esphome_ns.namespace("remote_receiver")
RemoteReceiverComponent = remote_receiver_ns.class_(
    "RemoteReceiverComponent", remote_base.RemoteReceiverBase, cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RemoteReceiverComponent),
        # unused here (frames arrive whole), but climate_ir platforms such as gree_ext validate it
        cv.Optional(CONF_IDLE, default="40ms"): cv.positive_time_period_microseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
