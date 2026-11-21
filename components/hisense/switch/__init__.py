import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from ..climate import CONF_HISENSE_ID, hisense_ac_ns, AirConditioner

AirConditionSwitch = hisense_ac_ns.class_(
    "AirConditionSwitch",
    cg.Component,
    switch.Switch,
)
Feature = AirConditioner.enum("Feature", True)

# config key -> (Feature enum member, icon)
SWITCH_TYPES = {
    "hisens_display": ("FEATURE_DISPLAY", "mdi:monitor"),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_HISENSE_ID): cv.use_id(AirConditioner),
    }
).extend(
    {
        cv.Optional(key): switch.switch_schema(AirConditionSwitch, icon=icon)
        for key, (_, icon) in SWITCH_TYPES.items()
    }
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_HISENSE_ID])

    for key, (feature, _) in SWITCH_TYPES.items():
        if conf := config.get(key):
            var = await switch.new_switch(conf)
            await cg.register_component(var, conf)
            cg.add(var.set_parent(paren))
            cg.add(var.set_feature(getattr(Feature, feature)))
            cg.add(paren.set_feature_switch(getattr(Feature, feature), var))
