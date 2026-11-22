import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import ENTITY_CATEGORY_CONFIG

from ..climate import CONF_HISENSE_ID, hisense_ac_ns, AirConditioner

AirConditionSleepProfileSelect = hisense_ac_ns.class_(
    "AirConditionSleepProfileSelect",
    cg.Component,
    select.Select,
)

CONF_SLEEP_PROFILE = "sleep_profile"
# Profiles 1..4 of the unit's sleep function, in protocol order (stock firmware
# names: general, aged, younger, child).
SLEEP_PROFILES = ["General", "Elderly", "Young", "Child"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_HISENSE_ID): cv.use_id(AirConditioner),
        cv.Optional(CONF_SLEEP_PROFILE): select.select_schema(
            AirConditionSleepProfileSelect,
            icon="mdi:power-sleep",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
    }
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_HISENSE_ID])
    if conf := config.get(CONF_SLEEP_PROFILE):
        var = await select.new_select(conf, options=SLEEP_PROFILES)
        await cg.register_component(var, conf)
        cg.add(var.set_parent(paren))
        cg.add(paren.set_sleep_profile_select(var))
