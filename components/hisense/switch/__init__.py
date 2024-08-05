from typing import Literal

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.cpp_helpers import gpio_pin_expression
from esphome.components import climate, sensor, uart, remote_transmitter, switch
from esphome.const import (
    CONF_FLOW_CONTROL_PIN,
    CONF_ID,
    CONF_ADDRESS,
    CONF_DISABLE_CRC,
    CONF_DISPLAY,
)
from esphome import pins
from pprint import pprint
from ..climate import CONF_HISENSE_ID, hisense_ac_ns, AirConditioner

AirConditionSwitch = hisense_ac_ns.class_(
    "AirConditionSwitch",
    cg.Component,
    switch.Switch,
)

ADD_SWITCH = "hisens_display"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_HISENSE_ID): cv.use_id(AirConditioner),
        cv.Optional(ADD_SWITCH): switch.switch_schema(
            AirConditionSwitch,
        ),
    }
)


def validate_visual(config):
    return config


async def to_code(config):
    paren = await cg.get_variable(config[CONF_HISENSE_ID])

    if conf := config.get(ADD_SWITCH):
        var = await switch.new_switch(conf)
        cg.add(var.set_parent(paren))
