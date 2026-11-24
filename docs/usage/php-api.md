---
eyebrow: 'touch-cst816s · PHP API'
lede:    'Constructor, onboard(), point(), points(), maxPoints(), gesture() and events() of Baremetal\I2c\Driver\Cst816s.'
see_also:
  - { href: '../overview.md', meta: 'Overview' }
  - { href: '../recipes/touch-events.md', meta: 'Recipe' }
  - { href: 'package:php-baremetal/ext-input#docs/usage/events.md', meta: 'Events', label: 'Touch events' }
prev: { label: 'CST816S touch controller', href: '../overview.md' }
next: { label: 'React to touches', href: '../recipes/touch-events.md' }
---

# Cst816s

<!-- @method name="new Cst816s(Bus $bus, int $address = 0x15, int $int = -1, int $rst = -1, int $width = 240, int $height = 240, bool $mirror_x = false, bool $mirror_y = false, bool $swap_xy = false, bool $double_tap = true)" -->

Attaches the chip. With `$rst` it pulses the reset line low for 20 ms and waits 80 ms. `$int` is the INT pin
(needed by `events()`). `$width` and `$height` (1 to 4096) are the screen size; the flags turn the chip's axes
into screen axes, swap first, then mirror. `$double_tap` turns the chip's double tap detection on (default) or off:
with it on `Gesture` `DoubleTap` is raised, and a single tap (`Tap`) comes about 260 ms after the release; with it
off `Tap` is immediate and `DoubleTap` never comes. The setting is written at once after a reset (`$rst`); otherwise
it is applied by the first read that succeeds (a touch wakes the sleeping chip), so a double tap may not be
recognised before that first touch is read. Throws `\ValueError` for a size outside 1 to 4096 or an address
outside `0x08` to `0x77`, and `\Exception` (`cannot init cst816s at 0x..: ...`) when the device cannot be attached.

<!-- @method name="Cst816s::onboard(string $block = 'touch'): Cst816s" -->

The controller described by a block of the board, on the bus of the board's `i2c` block. The block has `type`
`"cst816s"`, `width` and `height`, and optionally `address`, `int`, `rst`, `mirror-x`, `mirror-y`, `swap-xy` and `double-tap` (bool, default `true`).
Throws `\RuntimeException` when the board has no such block, the type differs, or `width` or `height` is missing.

<!-- @method name="point(): ?array" returns="?array" -->

Reads the chip: `['x' => int, 'y' => int]` in screen coordinates, or `null` with no finger or when the read fails
(the chip is asleep).

<!-- @method name="points(): array" returns="array" -->

The same read as `point()`, in the form of `Baremetal\Input\Touch::points()`: `[]` with no finger or when the read
fails, otherwise `[['id' => 0, 'x' => int, 'y' => int]]`. The CST816S tracks one finger, so its id is always 0.

<!-- @method name="maxPoints(): int" returns="int" -->

Returns 1: the CST816S reports one finger.

<!-- @method name="gesture(): ?Baremetal\Input\GestureType" returns="?GestureType" -->

Reads the gesture the chip holds now, or `null` when it holds none or the read fails. Swipe directions are the
ones on the screen: `mirror_x` swaps left and right, `mirror_y` swaps up and down, and `swap_xy` turns up into
left, down into right and back.

<!-- @method name="events(): Baremetal\Source" returns="Baremetal\Source" -->

Turns on the events described in [Touch events](package:php-baremetal/ext-input#docs/usage/events.md). Needs
`$int`: without it, `\ValueError` `Cst816s::events(): needs the INT pin (int:)`. It can be called again: it
returns a new handle on the same source.

The service reads the 6-byte report at each falling edge of INT and raises (always with `$id` 0: one finger):

| Change | Event |
| --- | --- |
| no finger, then a finger | `Pressed` |
| the finger moves to another point | `Moved` |
| a finger, then none | `Released` with the last point |
| a gesture code that differs from the previous report's and is not 0 (with double tap on, a tap code can arrive in a report with no finger, after the release) | `Gesture` |

With `$double_tap` on, after a release the service also reads once or twice more, 350 ms later, to catch the late
tap or double tap code even if INT does not pulse for it.

After a report with the finger down the service reads again 50 ms later; if that read sees no finger, `Released`
is raised, so a chip that gives no pulse at release still ends the touch. If a read fails while the finger is
down (the chip fell asleep), `Released` is raised with the last point.
