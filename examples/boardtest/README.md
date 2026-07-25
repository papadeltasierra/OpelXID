# OpelXID Board Test Example

This example is a hardware probing utility for a voltmeter.

It does not use the Opel MID protocol. It only drives GPIO levels so you can verify wiring, level-shifter behavior, and OE control.

## Behavior

- Every 1 second, SDA, SCL, and MRQ are toggled together between low and high.
- Every 10 seconds, OE is toggled between enabled and disabled.
- GPIOs are configured as strong push-pull outputs (`GPIO_MODE_OUTPUT`) for stable DC voltage checks with a voltmeter.
- Internal pull-ups and pull-downs are disabled.

## Configuration

Run `idf.py menuconfig` and open:

- `OpelXID Board Test Configuration`

Set:

- SDA GPIO pin
- SCL GPIO pin
- MRQ GPIO pin
- OE GPIO pin
- OE polarity (`OE is active-high`)

## Build and Run

```bash
cd examples/boardtest
idf.py build
idf.py flash monitor
```

Watch logs for current driven states while probing pins with your meter.
