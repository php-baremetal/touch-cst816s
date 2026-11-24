---
eyebrow: 'Package · Driver'
lede:    'The CST816S touch controller as a PHP class: the point on demand, the chip gestures, and interrupt-driven touch events. Registers into ext-i2c and implements the Touch interface of ext-input.'
see_also:
  - { href: './usage/php-api.md', meta: 'PHP API' }
  - { href: './recipes/touch-events.md', meta: 'Recipe' }
  - { href: 'package:php-baremetal/ext-input', meta: 'Extension', label: 'ext-input' }
  - { href: 'package:php-baremetal/ext-i2c', meta: 'Extension', label: 'ext-i2c' }
prev: { label: 'No previous page', href: '#' }
next: { label: 'Cst816s', href: './usage/php-api.md' }
---

# CST816S touch controller

touch-cst816s adds the class `Baremetal\I2c\Driver\Cst816s`. `point()` and `gesture()` read the chip when you
ask; `events()` turns on `Pressed`, `Moved`, `Released` and `Gesture` events served from the chip's INT line.

| Property | Value |
| --- | --- |
| Class | `Baremetal\I2c\Driver\Cst816s`, extends `Baremetal\I2c\Device` |
| Capability | `Baremetal\Input\Touch` (ext-input) |
| Default address, clock | `0x15`, 400 kHz |
| Fingers | one (`maxPoints()` is 1, the finger's id is 0) |
| Gestures | swipe up, down, left, right, tap, double tap, long press (codes `0x01` to `0x05`, `0x0B`, `0x0C`) |

## Requirements

| Requires | Why |
| --- | --- |
| `php-baremetal/ext-i2c` | The `Bus` and `Device` classes, the `i2c_drivers` registry, and the interrupt service. |
| `php-baremetal/ext-input` 1.1 or later | The `Touch` interface, the event classes and `GestureType`. |
| `php-baremetal/ext-gpio` | Its neutral GPIO HAL drives the optional reset line (conductor adds the GPIO HAL of your SDK platform). |

## Coordinates

The driver returns screen coordinates: `0 <= x < width`, `0 <= y < height`, after `swap_xy`, `mirror_x` and
`mirror_y`, so they match the display. A point the chip reports outside the screen is brought inside.

## Notes

- One shared bus: the event service runs on the executor task, the task that owns the I2C bus, so it does not
  collide with other devices on the same bus (an IMU sampled in the background, for example).
- The chip sleeps by itself and wakes on touch; see [Cst816s](./usage/php-api.md) for what a failed read does.
- Constructing the class again for the same bus and address returns a handle to the same device; the first
  construction decides the pins, size and orientation.
