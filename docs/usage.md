You need device with working ESPhome and RS-485 Transceivers.

Example configuration:

```
# Generic ESPHome configuration example:

safe_mode:
  num_attempts: 3

rtl87xx:
  board: generic-rtl8710bn-2mb-468k

# Enable logging
logger:
  level: DEBUG
  # baud_rate: 0 # You may have to disable logger via serial.

# Enable Home Assistant API
api:
  password: "insert password here"

ota:
  - platform: esphome
    password: "insert password here"

wifi:
  ssid: "insert ssid here"
  password: "insert password here"

  # Enable fallback hotspot (captive portal) in case wifi connection fails
  ap:
    ssid: "AC Fallback Hotspot"
    password: "insert password here"

captive_portal:

esphome:
  name: ac-livingroom
  platformio_options:
    platform_packages:
      - framework-arduino-api @ https://github.com/hn/ArduinoCore-API#RingBufferFix
      # https://github.com/libretiny-eu/libretiny/issues/154
      # rtl8710bn used in AEH-W4G2 and this component seem to be affected.

# Usage of this component:

external_components:
  # You may copy components from this repo and use source as local folder
  #- source:
  #    type: local
  #    path: local_components
  - source:
      type: git
      url: https://github.com/pio2398/W4G2.git
    components: [hisense]

uart:
  id: mod_bus
  tx_pin: PA23 # Valid config for AEH-W4G2. You may have to change it for other devices.
  rx_pin: PA18
  baud_rate: 9600
  debug:
    direction: BOTH
    dummy_receiver: true
    after:
      delimiter: [0xF4, 0xFB]
    sequence:
      - lambda: |-
          UARTDebug::log_hex(direction, bytes, ' ');

climate:
  - platform: hisense
    name: Hisense
    uart_id: mod_bus
    flow_control_pin: PA14 # AEH-W4G2 using half duplex RS-485 chip. Remove if using full duplex chip.
    id: hisense_ac

sensor:
  - platform: hisense
    hisense_id: hisense_ac
    indoor_coil_temperature:
      name: Indoor Coil Temperature
  - platform: hisense
    hisense_id: hisense_ac
    outdoor_coil_temperature:
      name: Outdoor Coil Temperature
  - platform: hisense
    hisense_id: hisense_ac
    indoor_temperature:
      name: Indoor Temperature
  - platform: hisense
    hisense_id: hisense_ac
    indoor_humidity:
      name: Indoor Humidit
  - platform: hisense
    hisense_id: hisense_ac
    outdoor_temperature:
      name: Outdoor Temperature

switch:
  - platform: hisense
    hisense_id: hisense_ac
    hisens_display:
      name: Hisens Display Enable

```