# One-shot bridge: applies the Aqara ac_status from $AQARA_AC to a climate_ir entity,
# lets it transmit (printed by the remote_transmitter override), then exits.
import esphome.codegen as cg
from esphome.components import climate
import esphome.config_validation as cv
from esphome.const import CONF_ID

CONF_CLIMATE_ID = "climate_id"

aqara_ir_ns = cg.esphome_ns.namespace("aqara_ir")
AqaraIR = aqara_ir_ns.class_("AqaraIR", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AqaraIR),
        cv.Required(CONF_CLIMATE_ID): cv.use_id(climate.Climate),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], await cg.get_variable(config[CONF_CLIMATE_ID]))
    await cg.register_component(var, config)
