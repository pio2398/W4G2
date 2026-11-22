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
## Additional options and entities

```yaml
climate:
  - platform: hisense
    # ...
    # Optional. "auto" (default) detects the fan type from the unit, "3" or "5" forces it.
    fan_speeds: auto

sensor:
  - platform: hisense
    hisense_id: hisense_ac
    supply_voltage:       # mains voltage reported by the unit
      name: Supply Voltage
    power:                # raw "f_electricity" value from the stock firmware (unit unknown; diagnostic)
      name: Power
    compressor_frequency: # compressor output frequency in Hz (0 when stopped)
      name: Compressor Frequency
    discharge_temperature: # compressor discharge temperature in °C
      name: Discharge Temperature
    setpoint:             # diagnostic: setpoint as reported by the unit, for graphing
      name: Setpoint
    temperature_compensation: # diagnostic: auto/dry compensation, raw value (encoding unknown)
      name: Temperature Compensation

text_sensor:
  - platform: hisense
    hisense_id: hisense_ac
    faults:               # "OK" or a list of active fault flags
      name: Faults
    identity:             # device type, product type, software and protocol version
      name: AC Identity

binary_sensor:
  - platform: hisense
    hisense_id: hisense_ac
    remote_used:          # diagnostic: unit was controlled by its IR remote or keys
      name: Remote Used

switch:
  - platform: hisense
    hisense_id: hisense_ac
    eco:
      name: Eco
    turbo:
      name: Turbo
    quiet:
      name: Quiet
    sleep:
      name: Sleep
    beep:                 # optional; default on, restored across reboots
      name: Beep

number:
  - platform: hisense
    hisense_id: hisense_ac
    timer_on:             # minutes until the unit turns on; 0 cancels. Shows remote timers too
      name: Timer On
    timer_off:            # minutes until the unit turns off; 0 cancels
      name: Timer Off

select:
  - platform: hisense
    hisense_id: hisense_ac
    sleep_profile:        # General, Elderly, Young or Child; used by the sleep switch
      name: Sleep Profile
```

`sleep_profile` picks which of the unit's four sleep curves the `sleep` switch turns on
(control byte 17 = `(profile << 1) | 1`, status byte 15 = `profile << 1`). Changing it while
sleep is on switches the unit to the new profile. While sleep is on, the select shows the
profile the unit reports, so a profile chosen with the remote shows up too. The last choice is
restored at boot.

`timer_on` / `timer_off` set the unit's own on and off timers, in minutes from now (up to
23 h 59 min). Setting 0 cancels the timer. They show the time left, including for timers set
with the remote. The unit counts down once a minute, so a timer can fire up to a minute early.

`remote_used` reads status byte 36 bit 3, which other implementations describe as "IR remote
or key control before this command". It has never been seen set on product type 104 (it stays
off through remote commands), but may work on other units.

`beep` is a local setting, not a unit state: it sets byte 23 bit 2 of the commands sent from
ESPHome, which asks the unit to beep when it accepts the command (the stock module always sets
it). Turning it off sends that byte as `00`: verified on a unit, the command still applies
and the unit stays silent. The unit does not report it, so it is restored from
flash at boot (`restore_mode`, default `RESTORE_DEFAULT_ON`).

### Unit identification

At start-up the component asks the indoor unit for its version (`07 01`), device type
(`0A 04`) and capability bits (`66 40`), the same read-only queries the stock WiFi module
sends. The capability bits are turned into the stock firmware's "product type" with a table
ported from that firmware (`capabilities.h`). These values select:

* 3-speed (values 2/3/4) or 5-speed (values 5..9) fan encoding. 3-speed is used for split
  units with protocol <= 532 or product types 102/103/106/107.
* The power format: status bytes 142-143 for protocol >= 534, or bytes 53-55 for older units.
  Bytes 142-143 are little-endian (142 is the low byte): on the units seen so far they repeat
  bytes 53-54 (big-endian) byte-swapped, e.g. 53:54 = `06 40` and 142:143 = `40 06` = 1600.

The result is logged at boot and in `dump_config`. You can also see it with the `identity`
text sensor. If your unit reports something unexpected, please include that line in issues.

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
* The climate action comes from the compressor (bytes 39/41) and the fan status. Fan code 0
  means the indoor fan is stopped (when off, in transitions, and during pre-heat).

* The unit answers every control command (0x65) with a full status frame in the same layout
  as the 0x66 reply. The component uses it to confirm changes within about 0.5 s.
* That reply can still show the old display state. Switch features are confirmed by the next poll.
* The bus is half duplex. Commands are queued and sent only when the previous exchange has
  finished, and changes made in quick succession are merged.
* The display is reported off while the unit is off and comes back on at power-on. The display
  bits are therefore sent only when the display switch changes.
* Turbo makes the unit set the fan to high and the setpoint to 16 °C. Sleep drops the fan to low.
  This is the unit's own behaviour.

## Protocol

The serial protocol (frame format, status and control fields, sources) is described in
[protocol.md](protocol.md).
