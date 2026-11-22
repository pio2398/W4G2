# Hisense AEH-W4G2 / indoor unit serial protocol

Technical specification of the serial protocol between a Hisense Wi-Fi module (AEH-W4G2 and
compatible) and the indoor unit of a Hisense air conditioner, as used by this component.

The protocol is not officially documented. Each field below gives its source (see
[Sources](#sources)) and whether it has been verified on a real unit. Fields without an **L**
come only from code or other projects and may not apply to every unit.

Reference unit for live verification: Hisense HAWV12KRD with an AEH-W4G2 module (RTL8710BN),
device type 1/1, product type 104, AC software version 272, protocol version 534.

## Contents

1. [Sources](#sources)
2. [Physical layer](#physical-layer)
3. [Frame format](#frame-format)
4. [Byte numbering](#byte-numbering)
5. [Message types](#message-types)
6. [Status reply (0x66 / 0x65)](#status-reply-0x66--0x65)
7. [Control command (0x65)](#control-command-0x65)
8. [Identification queries](#identification-queries)
9. [Behaviour notes](#behaviour-notes)
10. [Differences between implementations](#differences-between-implementations)
11. [References](#references)

## Sources

| Key | Source |
|---|---|
| **FW** | Stock AEH-W4G2 firmware (build 2022-06-30, module version S1798.6.02.09.00), disassembled. Function addresses below are for its OTA1 image |
| **S** | straga, *hisense_ac_xm_protocol*: bit-field tables for the status and control frames (`src/protocol/air-condition_msg.c`) |
| **K** | kadam12g, *AEH-W4G2-ESPHome* |
| **W** | AndrewDemsDS, *hisense-w41h1* (reverse engineering of the W41H1 module firmware) |
| **A** | akrabi, *hisense_ac_esphome* |
| **P** | pio2398, *W4G2* previous commits up to base `e0a2eab` |
| **L** | Verified live on the reference unit |

straga's tables give bit positions as `{end bit, length}`, numbered MSB first. Status bit 0 is
the MSB of `P[14]` and control bit 0 is the MSB of `T[16]` (see [Byte numbering](#byte-numbering)).
So a field ending at bit `e` with length `n` occupies bits `e-n+1 .. e`, and bit `b` is bit
`7 - b % 8` of byte `14 + b / 8` (status) or `16 + b / 8` (control).

## Physical layer

| Item | Value | Source |
|---|---|---|
| Interface | RS-485, half duplex | FW P L |
| Settings | 9600 baud, 8N1 | P L |
| AEH-W4G2 pins (RTL8710BN) | TX `PA23`, RX `PA18`, RS-485 direction `PA14` | P L |
| Roles | The module is the master: the indoor unit only answers requests | FW L |

## Frame format

```
F4 F5 | dir | 40 | len | 00 00 01 01 FE 01 00 00 | cmd | sub | data ... | csum_hi csum_lo | F4 FB
```

| Field | Size | Description | Source |
|---|---|---|---|
| `F4 F5` | 2 | Start of frame | FW P L |
| `dir` | 1 | `00` = module to indoor unit, `01` = indoor unit to module | FW L |
| flags | 1 | `40` = 2-byte checksum. Bit 5 would mean a 16-bit length (not seen) | FW |
| `len` | 1 | Number of bytes after `len` up to and including the last data byte (excludes checksum and trailer) | FW L |
| header | 8 | `00 00 01 01 FE 01 00 00`, the same in every frame seen | FW P L |
| `cmd` `sub` | 2 | Message type, see [Message types](#message-types) | FW L |
| data | len − 10 | Message data | |
| checksum | 2 | 16-bit sum of every byte from `dir` up to the last data byte, sent high byte first | FW P L |
| `F4 FB` | 2 | End of frame | FW P L |

### Byte stuffing

Every `F4` byte between the start and end markers, checksum included, is sent as `F4 F4`.
The receiver drops the second `F4` of each pair. An `F4 FB` whose `F4` is the second half of an
`F4 F4` pair is data, not the end of the frame. `len` and the checksum are computed over the
unstuffed bytes. (FW: TX stuffing at `0x8069a18`, RX state machine at `0x806a014`, unstuffing
at `0x80659a4`. Implemented by this component; K and P (previously) do not stuff.)

Without stuffing, a frame whose data or checksum contains `F4` fails its checksum on receive.
On transmit, the indoor unit silently drops it.

### Example

Status poll, `len` = 0x0C (12 bytes: 8 header + `66 00 00 00`), checksum `01 B3`:

```
F4 F5 00 40 0C 00 00 01 01 FE 01 00 00 66 00 00 00 01 B3 F4 FB
```

## Byte numbering

Two index conventions are used below. Both come from the original W4G2 code.

* **`P[n]`**: received frames, counted from `dir` = `P[0]`, i.e. frame byte `n + 2`.
  `P[11]` is `cmd`, `P[12]` is `sub`, the first status field is `P[14]`.
* **`T[n]`**: transmitted frames, counted from the `F4` of the start marker = `T[0]`.
  `T[13]` is `cmd`, the first control field is `T[16]`.

Both are indexes into the unstuffed frame.

## Message types

| `cmd sub` | Direction | Data | Meaning | Source |
|---|---|---|---|---|
| `66 00` | to unit | `00 00` | Status request. The unit answers with a `66 00` status reply | FW P L |
| `66 00` | from unit | status | Status reply, 141 bytes long (`len` = 0x8D) on the reference unit | FW P L |
| `65 00` | to unit | control | Control command, `len` = 0x29 (46-byte frame before checksum) | FW P L |
| `65 00` | from unit | status | Reply to a control command, in the status reply layout | L |
| `07 01` | both | | Version query | FW L |
| `0A 04` | both | | Device type query | FW L |
| `66 40` | both | | Capability query | FW L |
| `1E ..` | to unit | | Wi-Fi status sent periodically by the stock module. Not needed by the reference unit | FW K |

The stock firmware also has product-type and "num30" queries, not documented here.

## Status reply (0x66 / 0x65)

Bits are numbered 7 (MSB) to 0. "This unit" values are from the reference unit.

| Field | Location | Encoding | Source |
|---|---|---|---|
| Fan speed | `P[14] >> 1` | 0 = indoor fan stopped, 1 = auto, 5..9 = Lower, Low, Medium, High, Higher. 3-speed units use 2/3/4 (see [3-speed units](#product-type-gates)) | FW S L |
| Sleep profile | `P[15] >> 1` | 0 = off, 1 = General, 2 = Elderly, 3 = Young, 4 = Child (stock names: general, aged, younger, child) | FW S W K L |
| Mode | `P[16] >> 4` | 0 fan, 1 heat, 2 cool, 3 dry, 4-7 auto | FW S L |
| Power | `P[16]` bit 3 | 1 = on | FW S L |
| Up/down swing | `P[16]` bit 1 | Copy of `P[33]` bit 7 | S K L |
| Setpoint | `P[17]` | °C | FW S L |
| Indoor temperature | `P[18]` | °C. Values above 128 are treated as 0 | FW S L |
| Indoor coil temperature | `P[19]` | °C | S L |
| Humidity setpoint | `P[20]` | %. `80` = not supported (this unit) | S |
| Indoor humidity | `P[21]` | %. `80` = no sensor (this unit) | FW S L |
| "Feel" temperature | `P[22]` | IFEEL temperature from the remote. `00` on this unit | S |
| Feel compensation / feel on | `P[23]` bits 7-3 / bit 1 | `01` on this unit | S |
| Temperature compensation | `P[24] >> 4` | Auto/dry mode offset. Encoding not confirmed (the remote offers −7..+7). Seen 6 and 8 in cool mode, 0 otherwise | FW S W L |
| °F / °C | `P[24]` bit 1 | | FW S |
| Countdown timer ("normal timer") | `P[25]` | Bit 7 = set, bits 6-1 = half hours. Stays `00` when timers are set with the remote | S L |
| Unit clock | `P[26] >> 3` hours, `P[27] >> 2` minutes | Stays `00 00` on this unit | S L |
| On timer | `P[28]`, `P[29]` | `P[28]` bit 1 = set, `P[28] >> 3` = hours, `P[29] >> 2` = minutes. **Time remaining**, counted down once a minute. Bits `P[28]` bit 0 and `P[29]` bit 0 are also 1 while set. `00 00` = no timer | S L |
| Off timer | `P[30]`, `P[31]` | Same layout as the on timer | S L |
| Dry mode / vane position | `P[32]` bits 7-5 / bits 3-1 | | FW S |
| Up/down swing | `P[33]` bit 7 | | FW S K L |
| Left/right swing | `P[33]` bit 6 | | S K L |
| Natural wind / electric heat / energy saving | `P[33]` bits 5 / 4 / 3 | | S |
| Eco | `P[33]` bit 2 | | FW S K L |
| Turbo | `P[33]` bit 1 | | FW S K L |
| Dual mode | `P[33]` bit 0 | | S |
| Fresh air / air exchange / indoor clean / outdoor clean / smart eye | `P[34]` bits 7-3 | | S |
| Quiet | `P[34]` bit 2 | | FW S K L |
| Voice / smoke removal | `P[34]` bits 1 / 0 | | S |
| Display | `P[35]` bit 7 | Reported off while the unit is off | S K L |
| Panel / LED / temperature display / filter reset / vanes / power board | `P[35]` bits 6-0 | | S FW |
| Remote used | `P[36]` bit 3 | "IR remote or key control before this command". **Never set on the reference unit**, through several remote commands | S K L |
| Wi-Fi control | `P[36]` bit 2 | Set on this unit | S L |
| Normal / test model | `P[36]` bit 1 | | S |
| Indoor EEPROM online upgrade | `P[36]` bit 0 | Set on this unit | S L |
| Error flags A | `P[37]` | b7 indoor temp sensor, b6 indoor coil sensor, b5 humidity sensor, b4 water full, b3 indoor fan motor, b2 grille, b1 zero crossing, b0 indoor-outdoor communication | FW S |
| Error flags B | `P[38]` | b7 display, b6 keys, b5 Wi-Fi, b4 electrical, b3 indoor EEPROM | FW S |
| Compressor frequency | `P[39]` running/target, `P[40]` target, `P[41]` output | Hz. `P[41]` ramps to 0 when stopped | S K L |
| Outdoor temperature | `P[42]` | °C, signed. Reads `EC` (−20 °C) briefly after mains power returns | S K W L |
| Outdoor coil temperature | `P[43]` | °C, signed | S K L |
| Discharge temperature | `P[44]` | °C. 20 °C idle, about 30 °C a few minutes into cooling | S K L |
| Target discharge temperature | `P[45]` | `00` on this unit | S |
| EEV opening | `P[46]` | `00` on this unit | S |
| Supply voltage | `(P[47] << 8) \| P[48]` | V | FW S L |
| Power (old format) | `P[53]`, `P[54]`, `P[55]` | Units with protocol < 534: `P[53] * 3` if `P[55] != 0`, else `(P[53] << 8) + P[54]`, else `P[53]`. On the reference unit `P[53]:P[54]` carries the same value as the new format | FW L |
| IUV | `P[58]` | ≈ power / 100 on this unit | S K |
| 4-way valve / outdoor unit running / outdoor fan | `P[59]` bit 4 / bit 3 / bits 2-0 | | S |
| Thermostat off / defrost | `P[60]` bit 4 / bit 1 | Never set on this unit, even with the compressor idle at setpoint | S L |
| Error flags C | `P[62]` | b6 outdoor EEPROM, b5 outdoor coil sensor, b4 discharge sensor, b3 outdoor temp sensor | FW S |
| Over-cold / over-hot protection | `P[64]` bit 4 | The stock firmware reads the same bit for both | FW |
| Fan percentage | `P[72]` | | FW |
| Swing follow | `P[78]` bits 5-4 | | FW |
| Demand response / DAL / TALR | `P[81]` bits 7 / 6 / 5 | | FW |
| Swing direction | `P[82]` bits 2-0 | | FW |
| TMS | `P[82]` bit 6 | | FW |
| Power (new format) | `(P[143] << 8) \| P[142]` | **Little-endian.** Used for protocol ≥ 534. Unit not confirmed (stock name `f_electricity`): about 1000-1800 while cooling, about 400 fan only, 0 off | FW W L |

The stock firmware's status decoder is `handle_aircrond_increment_result` at `0x806c67a`.

## Control command (0x65)

Each field carries a value and a "valid" bit. Fields whose valid bit is 0 are left unchanged,
so a command may carry any subset of fields and all other bytes are `00`.

| Field | Location | Encoding | Source |
|---|---|---|---|
| Fan speed | `T[16]` | `(v << 1) \| 1`, v as in the status reply: `03` auto, `0B`..`13` Lower..Higher. 3-speed units: v = 2/3/4 | FW S L |
| Sleep | `T[17]` | `(profile << 1) \| 1`: `01` off, `03` / `05` / `07` / `09` profiles 1-4 | FW W L |
| Mode | `T[18]` | `(v << 5) \| 0x10`, v as in the status reply (4 = auto) | FW S L |
| Power | `T[18]` | `(on << 3) \| 0x04`. Power off: `T[18]` = `04` | FW S L |
| Setpoint | `T[19]` | `(°C << 1) \| 1` | FW S L |
| °F / °C | `T[23]` | `(v << 1) \| 1` | FW |
| Temperature compensation | `T[23]` | `(v << 4) \| 0x08` | FW W |
| Beep | `T[23]` bit 2 | `1` = the unit beeps when it accepts the command (the stock module always sets it). With `0` the command applies silently. Does not affect beeps for IR remote commands | FW L |
| Unit clock | `T[25]`, `T[26]` | `(hours << 3) \| 1`, `(minutes << 2) \| 1`. Sent together with a timer | S L |
| On timer | `T[27]`, `T[28]` | `T[27]` = `(hours << 3) \| 0x04 \| (on << 1) \| 0x01`, `T[28]` = `(minutes << 2) \| 0x01`. Cancel: `T[27]` = `01` | S L |
| Off timer | `T[29]`, `T[30]` | Same layout as the on timer | S L |
| Swing angle | `T[31]` | `((v << 1) & 0x0E) \| 1` | FW |
| Up/down swing | `T[32]` | `(on << 7) \| 0x40` | FW S K L |
| Left/right swing | `T[32]` | `(on << 5) \| 0x10`. Both: `F0`, off: `50`, up/down only: `D0`, left/right only: `70` | S K P L |
| Turbo | `T[33]` | `(on << 3) \| 0x04` | FW S K L |
| Eco | `T[33]` | `(on << 5) \| 0x10` | FW S K L |
| Quiet | `T[35]` | `(on << 5) \| 0x10` | FW S K L |
| Display | `T[36]` | `C0` on, `40` off. Not in the stock command builder | S P L |
| Fan percentage | `T[40]` | `(percent << 1) \| 1`, 0-100. Device type 1 maps fan 5..9 to 5/10/50/80/90 % | FW |
| Swing follow | `T[41]` | `((v << 2) & 0x0C) \| 0x02` | FW |
| TMS | `T[47]` | `((v << 5) & 0x20) \| 0x10` | FW |
| Swing direction | `T[47]` | `((v << 1) & 0x0E) \| 1` | FW |
| Demand response | `T[59]` | `(v << 7) \| 0x40` | FW |
| DAL | `T[59]` | `(v << 5) \| 0x10` | FW |
| TALR | `T[59]` | `(v << 3) \| 0x04` | FW |

`T[47]` and above are in an extended block that the stock firmware fills in only when one of
those fields is used. The frame length in that case has not been verified.

The stock firmware's command builder is `get_dev_control_cmd` at `0x80648b0`.

### Timers

The stock app sends a clock time for the timer together with the current time for the unit
clock (straga `xm_KTKT` / `xm_KTGT`). The reference unit reports timers as time remaining and
never reports a clock. Sending the clock as 00:00 (`T[25]` `T[26]` = `01 01`) and the timer as
the delay works either way (L):

| Command | `T[25]`..`T[30]` | Result |
|---|---|---|
| On in 2 min | `01 01 07 09 00 00` | `P[28] P[29]` = `03 09`, unit turned on after 108 s |
| Off in 4 min | `01 01 00 00 07 11` | `P[30] P[31]` = `03 11` |
| Cancel off timer | `00 00 00 00 01 00` | `P[30] P[31]` = `00 00` |
| Off in 1 min | `01 01 00 00 07 05` | Unit turned off after 42 s |

The unit counts down on its own minute tick, so a timer can fire up to a minute early.
Timers set with the IR remote appear in the same status bytes.

### Sleep profiles

What each profile does is not reported by the unit. A Hisense manual for a model with four
sleep curves (AST-24UW4SDBTG10) describes them as below. That they match profiles 1-4 on other
models is an assumption. In all four the fan drops to low (seen live) and the unit stops after
8 hours.

| Profile | Cooling setpoint (heating is the mirror image) |
|---|---|
| 1 General | Rises by up to 2 °C over the first 2 h, then holds |
| 2 Elderly | +2 °C after 2 h, −1 °C after 6 h, −1 °C after 7 h |
| 3 Young | +1 °C after 1 h, +2 °C after 2 h, −2 °C after 6 h, −1 °C after 7 h |
| 4 Child | Holds steady |

Whether the shifted setpoint shows up in `P[17]` has not been checked.

### Quirks for older units

For AC protocol versions below 530 the stock firmware also sets `T[35] |= 0x30` for auto fan,
and for sleep forces fan 2 and sets `T[33] |= 0x14` and `T[35] |= 0x10`. On a mode change it
clears sleep, turbo and eco. (FW; not implemented by this component.)

## Identification queries

Read-only queries the stock module sends at start-up (FW). The component sends the same three
once at boot. Data shown from `cmd`; frame: `F4 F5 00 40 0B 00 00 01 01 FE 01 00 00 <data> csum F4 FB`.

| Query | Request | Reply on the reference unit | Meaning | Source |
|---|---|---|---|---|
| Version | `07 01 00` | `07 01 01 01 10 02 16` | AC software version = bytes 3:4 (272), protocol version = bytes 5:6 (534) | FW L |
| Device type | `0A 04 00` | `0A 04 01 01 01` | Type 1 / subtype 1 = air conditioner (21 = dehumidifier) | FW L |
| Capabilities | `66 40 00` | `66 40 01 09 04 D6 05 07 EA 01 70 48 80 80 00 …` | Capability bits, decoded to a product type (104 here) | FW L |

### Product-type gates

The stock firmware turns the capability bits into a "product type" that selects the app's
feature set. Its decoder is ported to `components/hisense/capabilities.h` (checked against the
original on 49,000 inputs). The firmware has no model-name table.

| Gate | Condition | Source |
|---|---|---|
| Product type range | Device type 1: 10 capability bits, products 100-113. Type 54: 2 bits, 200-203. Type 55: 6 bits, 300-311 | FW |
| 3-speed fan (2/3/4) | Device type 1 with protocol ≤ 532, or products 102/103/106/107. On protocol ≤ 532, status values 2/3/4 are shown as speeds 6/7/8 | FW |
| Power format | Protocol ≥ 534: `P[142]`/`P[143]`. Older: `P[53]`..`P[55]` | FW L |
| Fan percentage | Products 112/113/118/119 | FW |
| Swing direction/angle, TMS, temperature compensation | Products 108-119 | FW |
| Swing follow | Products 110-113, 116-119 | FW |
| Demand response / DAL / TALR | Products 304-311 | FW |
| Power and voltage reported to the cloud | Not for products 104-113, 200, 201, 301, 303, 400, 401. The reference unit (104) still reports voltage | FW L |

## Behaviour notes

All verified on the reference unit (L).

* The unit answers a control command within about 0.3 s with a `65 00` frame in the status
  layout. Fields that were just changed (display, eco, turbo, quiet, sleep, swing, timers) can
  still show the old value in that reply. The next `66 00` poll shows the new value.
* The bus is half duplex. Send a command only after the previous reply has been received.
  Several fields can be changed in one command.
* Turbo makes the unit set the fan to high and the setpoint to 16 °C. Sleep drops the fan to low.
* The display is reported off while the unit is off and comes back on at power-on, so the
  display field should be sent only when it is meant to change.
* Swing values seen in `P[16]` / `P[33]`: off `28` / `00`, up/down `2A` / `80`, both `2A` /
  `C0`, left/right `28` / `40` (cool mode, power on).
* Fan code 0 in `P[14]` means the indoor fan is stopped: when off, during transitions and during
  pre-heat.

## Differences between implementations

| Project | Notes |
|---|---|
| straga, hisense_ac_xm_protocol | The most complete field list (Chinese field names). Every field decoded by this component agrees with it, except that it gives the clock-timer flags without saying they are a countdown |
| kadam12g, AEH-W4G2-ESPHome | Same bit positions. No byte stuffing. Fan names one step off (5 = "Low"), status `02` decoded as "Quiet" (it is auto). Compressor frequency from `P[40]`. `P[53]`/`P[54]`/`P[58]` labelled as currents and voltage. Turning off also sends mode auto. Timer flags read at `P[29]`/`P[31]` bit 0. Sends the `1E` heartbeat |
| AndrewDemsDS, hisense-w41h1 | Found the little-endian power bytes, the `EC` outdoor reading after power-up, and the stock command builders (`t_sleep`, `t_temp_compensate`) |
| akrabi, hisense_ac_esphome | Uses frame offsets (= `P[n]` + 2). Field positions agree |

## References

- pio2398, *W4G2*, previous commits up to base `e0a2eab`, PR #9 for swing:
  https://github.com/pio2398/W4G2
- straga, *hisense_ac_xm_protocol* (commit `96f355b`), `src/protocol/air-condition_msg.c`
  (GBK encoded): https://github.com/straga/hisense_ac_xm_protocol
- straga, *scrivo_project* (commit `c467f8c`): https://github.com/straga/scrivo_project
- kadam12g, *AEH-W4G2-ESPHome* (commit `de6743d`), `my_components/hisense_ac/`:
  https://github.com/kadam12g/AEH-W4G2-ESPHome
- AndrewDemsDS, *hisense-w41h1* (commit `bc562e6`), `reverse-engineering/docs/`:
  https://github.com/AndrewDemsDS/hisense-w41h1
- akrabi, *hisense_ac_esphome* (commit `ddbd66c`): https://github.com/akrabi/hisense_ac_esphome
- LibreTiny issues #366 (UART RX disabled after `UART_Init`) and #419 (RX ring buffer race):
  https://github.com/libretiny-eu/libretiny
- Hisense AST-24UW4SDBTG10 user manual (four sleep curves, clock timers):
  https://hisense.es/wp-content/uploads/2017/08/manual-AST-24UW4SDBTG10-1.pdf
- Hisense J1-91 remote controller instructions (single sleep curve, SUPER, IFEEL, timers):
  https://hisense.co.th/img/products/gallery/NzAxNzI5MTE/0eb748d60c5c70256637df5444a5ce2b.pdf
- Stock AEH-W4G2 firmware: disassembly of a flash dump of the module (build 2022-06-30). Not
  distributed here.
