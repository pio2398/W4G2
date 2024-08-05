import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_OUTDOOR_TEMPERATURE,
    CONF_POWER,
    CONF_HUMIDITY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_CURRENT_AC,
    ICON_FLASH,
    ICON_GAUGE,
    ICON_HEATING_COIL,
    ICON_PULSE,
    ICON_THERMOMETER,
    ICON_WATER_PERCENT,
    ICON_WEATHER_WINDY,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_PERCENT,
    UNIT_WATT,
)
from ..climate import (
    CONF_HISENSE_ID,
    AirConditioner,
)


SensorTypeEnum = AirConditioner.enum("SubSensorType", True)

CONF_INDOOR_COIL_TEMPERATURE = "indoor_coil_temperature"
CONF_OUTDOOR_COIL_TEMPERATURE = "outdoor_coil_temperature"
CONF_INDOOR_TEMPERATURE = "indoor_temperature"
CONF_INDOOR_HUMIDITY = "indoor_humidity"
CONF_OUTDOOR_TEMPERATURE = "outdoor_temperature"


SENSOR_TYPES = {
    CONF_INDOOR_COIL_TEMPERATURE: sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        icon=ICON_HEATING_COIL,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_OUTDOOR_COIL_TEMPERATURE: sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        icon=ICON_HEATING_COIL,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_INDOOR_TEMPERATURE: sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        icon=ICON_THERMOMETER,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_OUTDOOR_TEMPERATURE: sensor.sensor_schema(
        unit_of_measurement=UNIT_CELSIUS,
        icon=ICON_THERMOMETER,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    CONF_INDOOR_HUMIDITY: sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        icon=ICON_WATER_PERCENT,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_HUMIDITY,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_HISENSE_ID): cv.use_id(AirConditioner),
    }
).extend({cv.Optional(type): schema for type, schema in SENSOR_TYPES.items()})


async def to_code(config):
    paren = await cg.get_variable(config[CONF_HISENSE_ID])

    for type, _ in SENSOR_TYPES.items():
        if conf := config.get(type):
            sens = await sensor.new_sensor(conf)
            sensor_type = getattr(SensorTypeEnum, type.upper())
            cg.add(paren.set_sub_sensor(sensor_type, sens))
