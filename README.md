# touch-cst816s

A driver for the CST816S capacitive touch controller on I2C: one finger, plus the gesture code the chip works out
itself (swipes, tap, double tap, long press). It registers into ext-i2c's `i2c_drivers` registry, so a firmware
built with it has the class `Baremetal\I2c\Driver\Cst816s`: it extends `Baremetal\I2c\Device`, implements
`Baremetal\Input\Touch` of [ext-input](package:php-baremetal/ext-input), reads the point on demand with
`point()` and `gesture()`, and with the INT pin wired raises `Pressed`, `Moved`, `Released` and `Gesture` events
from an interrupt service on the executor task.

## Install

```bash
composer require php-baremetal/touch-cst816s
```

ext-i2c, ext-input and ext-gpio (reset line) come with it.

## Example

```php
<?php
use Baremetal\I2c\Bus;
use Baremetal\I2c\Driver\Cst816s;

$touch = new Cst816s(new Bus(sda: 11, scl: 10), int: 4, rst: 1, width: 360, height: 360);

$p = $touch->point();          // ['x' => 120, 'y' => 200], or null with no finger
$touch->events();              // Pressed / Moved / Released / Gesture, see ext-input
```

(The pin numbers are an example: use the INT and reset pins of your board.)

## Pins

- I2C: SDA and SCL of the bus the chip is on, address `0x15`.
- `int`: the INT line, low at each report. Needed for `events()`; without it only the synchronous reads work.
- `rst`: the reset line, optional. With it the constructor pulses it low for 20 ms and waits 80 ms.

On a board whose description has a `touch` block, `Cst816s::onboard()` takes everything from the board.

## Orientation

`width` and `height` are the screen size; `mirror_x`, `mirror_y` and `swap_xy` turn the chip's coordinates into
screen coordinates the way the display does (swap first, then mirror), and keep them inside the screen. Swipe
directions follow the same transform: with `mirror_x` a swipe to the left of the chip is a swipe to the right on
the screen.

## Double tap

`double_tap` (default `true`; board key `double-tap`) sets bit 0 (EnDClick) of the chip's MotionMask register.
With it on the chip reports `DoubleTap`, but it also decides whether a touch was a single tap only after waiting
for a second one: `Tap` is raised about 260 ms after the release, not at once. With `double_tap: false` `Tap` is
immediate and `DoubleTap` is never raised.

The chip sleeps when idle and does not answer on I2C then. With `rst` the chip is awake after the reset and the
setting is written in the constructor. Without it (or if the write fails) the setting stays pending and is written
by the first successful read: the first report of the event service, or the first `point()` or `gesture()` that
reaches the chip. Until then the chip keeps its power-on default (double tap off), so pass `rst` when the very
first touch must already be able to be a double tap. Because the tap code comes late, after a release the service
looks once or twice more, 350 ms later, even without an INT pulse.

## Sleep

The chip puts itself to sleep and wakes on a touch. A synchronous read while it sleeps can fail: `point()` and
`gesture()` return `null` instead of throwing. In the event service, a failed read while a finger is down raises
`Released` with the last point.

## Events

`events()` needs `int:` (`\ValueError` `Cst816s::events(): needs the INT pin (int:)` otherwise). Each falling edge
of INT makes the executor read the 6-byte report and raise what changed. While a finger is down the service
re-reads after 50 ms, so a `Released` is raised even when the chip gives no pulse at the release.

## Documentation

- [Overview](docs/overview.md)
- [Cst816s](docs/usage/php-api.md)
- [React to touches](docs/recipes/touch-events.md)

## License

MIT.
