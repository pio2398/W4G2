import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import DEVICE_CLASS_DURATION, UNIT_MINUTE

from ..climate import CONF_HISENSE_ID, hisense_ac_ns, AirConditioner

AirConditionTimerNumber = hisense_ac_ns.class_(
    "AirConditionTimerNumber",
    number.Number,
)

CONF_TIMER_ON = "timer_on"
CONF_TIMER_OFF = "timer_off"
TIMER_MAX_MINUTES = 24 * 60 - 1


def timer_schema(icon):
    return number.number_schema(
        AirConditionTimerNumber,
        icon=icon,
        unit_of_measurement=UNIT_MINUTE,
        device_class=DEVICE_CLASS_DURATION,
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_HISENSE_ID): cv.use_id(AirConditioner),
        # Minutes until the unit turns on / off. Shows the time left on a timer set
        # with the remote too. 0 = no timer; setting 0 cancels it.
        cv.Optional(CONF_TIMER_ON): timer_schema("mdi:timer-play-outline"),
        cv.Optional(CONF_TIMER_OFF): timer_schema("mdi:timer-stop-outline"),
    }
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_HISENSE_ID])
    for key, off in ((CONF_TIMER_ON, False), (CONF_TIMER_OFF, True)):
        if conf := config.get(key):
            var = await number.new_number(conf, min_value=0, max_value=TIMER_MAX_MINUTES, step=1)
            cg.add(var.set_parent(paren, off))
            cg.add(paren.set_timer_number(off, var))
