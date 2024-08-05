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

DEPENDENCIES = ["climate", "uart"]

hisense_ac_ns = cg.esphome_ns.namespace("hisense").namespace("ac")
AirConditioner = hisense_ac_ns.class_(
    "AirConditioner", uart.UARTDevice, cg.Component, climate.Climate
)
CONF_HISENSE_ID = "hisense_id"


CONFIG_SCHEMA = cv.All(
    climate.CLIMATE_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(AirConditioner),
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    cv.only_with_arduino,
)


def validate_visual(config):
    return config


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    await climate.register_climate(var, config)

    if CONF_FLOW_CONTROL_PIN in config:
        pin = await gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
        cg.add(var.set_flow_control_pin(pin))

    if CONF_DISPLAY in config:
        cg.add(var.set_display_state(config[CONF_DISPLAY]))
