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
## LibreTiny (RTL8710BN) UART issues: handled by the component

On RTL8710B the component works around two LibreTiny bugs itself. No YAML lambdas are needed:

* LibreTiny >= 1.12 (ESPHome 2026.x) never enables UART RX, so requests go out but no reply
  arrives (libretiny-eu/libretiny#366). The component re-enables RX in `setup()`.
* The Arduino RX ring buffer's element count is updated from both the interrupt and `read()`
  without locking. A lost update leaves the end of every frame unreadable, which looked like
  the component stalling for ~18 s after each reply (libretiny-eu/libretiny#419). The component
  masks the UART RX interrupt around each read.

The `framework-arduino-api` RingBufferFix `platform_packages` override is not needed. Current
builds do not apply it, and it does not fix the race above.

## Notes from live testing (AEH-W4G2, product type 104)

* `outdoor_temperature` and `outdoor_coil_temperature` are read from status bytes 42/43.
  Earlier versions read bytes 40/41, which are compressor frequencies.

* The unit answers every control command (0x65) with a full status frame in the same layout
  as the 0x66 reply. The component uses it to confirm changes within about 0.5 s.
* That reply can still show the old display state. Switch features are confirmed by the next poll.
* The bus is half duplex. Commands are queued and sent only when the previous exchange has
  finished, and changes made in quick succession are merged.
* The display is reported off while the unit is off and comes back on at power-on. The display
  bits are therefore sent only when the display switch changes.
