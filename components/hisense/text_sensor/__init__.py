import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from ..climate import CONF_HISENSE_ID, AirConditioner

CONF_FAULTS = "faults"
CONF_IDENTITY = "identity"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_HISENSE_ID): cv.use_id(AirConditioner),
        cv.Optional(CONF_FAULTS): text_sensor.text_sensor_schema(
            icon="mdi:alert-circle-outline",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # Device type, product type, software and protocol version reported by the unit.
        cv.Optional(CONF_IDENTITY): text_sensor.text_sensor_schema(
            icon="mdi:information-outline",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_HISENSE_ID])
    if conf := config.get(CONF_FAULTS):
        sens = await text_sensor.new_text_sensor(conf)
        cg.add(paren.set_faults_text_sensor(sens))
    if conf := config.get(CONF_IDENTITY):
        sens = await text_sensor.new_text_sensor(conf)
        cg.add(paren.set_identity_text_sensor(sens))
