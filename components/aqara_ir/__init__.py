# One-shot bridge for libirplug.so: applies the Aqara ac_status from $AQARA_AC to a climate_ir entity
# and lets it transmit, or decodes received timings from $AQARA_RX with it. See aqara_ir.h.
import esphome.codegen as cg
from esphome.components import climate
import esphome.config_validation as cv
from esphome.const import CONF_ID

CONF_CLIMATE_ID = "climate_id"
CONF_RECEIVER_ID = "receiver_id"

AUTO_LOAD = ["remote_receiver"]

aqara_ir_ns = cg.esphome_ns.namespace("aqara_ir")
AqaraIR = aqara_ir_ns.class_("AqaraIR", cg.Component)
# the injectable receiver from this repo's remote_receiver override
RemoteReceiverComponent = cg.esphome_ns.namespace("remote_receiver").class_("RemoteReceiverComponent")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AqaraIR),
        cv.Required(CONF_CLIMATE_ID): cv.use_id(climate.Climate),
        cv.Optional(CONF_RECEIVER_ID): cv.use_id(RemoteReceiverComponent),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], await cg.get_variable(config[CONF_CLIMATE_ID]))
    await cg.register_component(var, config)
    if CONF_RECEIVER_ID in config:
        cg.add(var.set_receiver(await cg.get_variable(config[CONF_RECEIVER_ID])))
