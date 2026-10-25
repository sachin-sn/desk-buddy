# Power, battery backup and charging

The buddy is designed to run from USB power. A battery is an optional backup so it
survives a power cut or being moved; this page records what the board can and cannot
do, and what to add.

## What the board has (verified)

Sources: Elecrow's Eagle schematic `ESP32 Display-1.28-V1.0.sch` and its Arduino
example, both in the
[Elecrow-RD/CrowPanel-1.28inch-HMI-ESP32-Rotary-Display repository](https://github.com/Elecrow-RD/CrowPanel-1.28inch-HMI-ESP32-Rotary-Display-240-240-IPS-Round-Touch-Knob-Screen),
plus the product page ("Power Input 5V/1A").

- **No battery connector, no charger IC, no battery-voltage sensing.** The only
  regulator is a buck converter (HX3406) that makes 3.3V from the 5V rail.
- **5V enters through J37**, the 1.25 mm 4-pin socket Elecrow calls the "5V charging
  interface" (the one the supplied USB cable plugs into). It is also the USB data path.
- Input power goes through a P-MOSFET (Q3) that a small transistor turns on when 5V is
  present on J37. Its body diode blocks reverse polarity.
- Three more switches matter to firmware: GPIO1 powers the LCD and touch controller,
  GPIO2 powers the +5V pins of the two external 1.25 mm connectors, GPIO46 is the
  backlight. `main.cpp` drives GPIO1 and GPIO2 HIGH at boot, as Elecrow's example does.

### J37 pin order (from the schematic nets)

| Pin | Signal |
|-----|--------|
| 1 | +5V (`VUSB_IN`) |
| 2 | USB D- |
| 3 | USB D+ |
| 4 | GND |

Check the pin-1 end of the plug against the board before you connect anything; the
schematic gives the pin numbers, not the mechanical orientation.

## What to add for a backup battery

Use one **single-cell Li-ion charge-and-boost module** with these properties:

1. USB-C (or micro-USB) **input** for the wall adapter.
2. **Charges and powers the load at the same time** (a "UPS" or load-sharing module).
3. **5V output**, at least 1A. The buddy's worst case is WiFi transmitting with the
   backlight and the five LEDs on.
4. **Cell protection** (over-charge, over-discharge, short circuit) on the module or the
   cell.

Wire the module's 5V output to J37 pin 1 and its ground to pin 4 (a 1.25 mm 4-pin
pigtail, leaving pins 2 and 3 unconnected). The wall adapter plugs into the module, not
into the board.

Flashing needs the data lines, so for firmware updates unplug the module's pigtail and
use the Elecrow USB cable on J37, then swap back. Do not connect both at once: two 5V
sources on the same rail fight, and the module's output would feed the computer's USB
port.

### Candidates

| Module | Good | Watch out |
|--------|------|-----------|
| **IP5306-based "5V 2A Type-C boost charging module"** (about ₹120-300, many Indian shops) | Cheapest. Charges at up to 2.1A while powering the load. Fits one 18650 or LiPo. | Basic versions **switch the output off if the load stays under about 50 mA for ~32 s**. Output **dips for a moment when the charger is plugged in or removed**, which can reboot the ESP32. Behaviour differs between genuine and clone chips. Clone boards have been reported to fail to wake after a day or two. Add a 470-1000 µF capacitor across the output. |
| **Adafruit PowerBoost 1000C** (about ₹2,200 at Robu when last checked, out of stock then) | True load-sharing, so the output does not drop when USB is removed. 1A boost, 1A charge, no idle shut-off, low-battery LED at 3.2V. | Micro-USB input only. Costs about ten times the IP5306 board. Check that the listing is genuine. |

The IP5306 module is the sensible first buy because the buddy normally draws more than
50 mA. If you see it switch off or reboot at the moment you unplug the adapter, move to a
load-sharing module such as the PowerBoost.

Avoid the very cheap TP4056 + boost-converter combination. The TP4056 has no power path,
so with a constant load it never sees the end-of-charge current and keeps the cell at
4.2V, which shortens its life.

### Battery cell

One protected 18650 (3.7V, 2000-3000 mAh) in a holder, from a reputable seller. A flat
LiPo works too if the connector polarity matches the module; reversed polarity on cheap
cells is common and destroys boards. Measure before connecting.

## Measure before you decide on anything else

Put a USB power meter between the adapter and the module and note the buddy's draw in
each state: eyes with the backlight at its standby level, clock screen, WiFi busy. Record
the numbers here. They decide whether the IP5306 shut-off is a risk and how long the cell
will last: runtime in hours is roughly the cell's mAh times 0.6, divided by the average
mA drawn at 5V (a 2500 mAh cell at 150 mA gives about 10 hours).

## Optional: telling the buddy it is on battery

Not built. Two ways, both listed in the backlog (M3-01):

- `PIN_VBUS_SENSE` (GPIO4 on the flat-flex connector): a 100k/150k divider on the
  adapter side of the module reads HIGH on mains and LOW on battery.
- An I2C fuel gauge (for example MAX17048) on the spare I2C header, GPIO38 and GPIO39,
  gives a real percentage.

Whether GPIO4 and the I2C header are physically reachable on your unit is not confirmed;
the FPC connector J3 and the 1.25 mm sockets are the places to look.
