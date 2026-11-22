import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from ..climate import CONF_HISENSE_ID, AirConditioner

CONF_FAULTS = "faults"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_HISENSE_ID): cv.use_id(AirConditioner),
        cv.Optional(CONF_FAULTS): text_sensor.text_sensor_schema(
            icon="mdi:alert-circle-outline",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_HISENSE_ID])
    if conf := config.get(CONF_FAULTS):
        sens = await text_sensor.new_text_sensor(conf)
        cg.add(paren.set_faults_text_sensor(sens))
