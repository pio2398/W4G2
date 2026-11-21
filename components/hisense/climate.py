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
CONF_FAN_SPEEDS = "fan_speeds"
FAN_SPEEDS = {"auto": 0, "3": 3, "5": 5}


# ESPHome API migration:
#   - CLIMATE_SCHEMA was deprecated in 2025.05 and removed in 2025.11.
#   - The new helper `climate.climate_schema(<Class>)` replaces it and also
#     auto-generates the component ID, so the explicit
#     `cv.GenerateID(): cv.declare_id(AirConditioner)` entry is no longer
#     required (and would actually collide with the auto-generated one).
#
# The try/except keeps the component buildable on older ESPHome releases too
# (anything prior to 2025.11 where `climate_schema` did not yet exist).
try:
    _BASE_CLIMATE_SCHEMA = climate.climate_schema(AirConditioner)
except AttributeError:
    # Fallback for ESPHome < 2025.11
    _BASE_CLIMATE_SCHEMA = climate.CLIMATE_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(AirConditioner),
        }
    )


CONFIG_SCHEMA = cv.All(
    _BASE_CLIMATE_SCHEMA.extend(
        {
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
            # "auto" detects 3- or 5-speed fans from the unit's protocol version
            # and capability bits, as the stock WiFi module does.
            cv.Optional(CONF_FAN_SPEEDS, default="auto"): cv.All(
                cv.string, cv.one_of(*FAN_SPEEDS, lower=True)
            ),
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

    cg.add(var.set_fan_speeds(FAN_SPEEDS[config[CONF_FAN_SPEEDS]]))
