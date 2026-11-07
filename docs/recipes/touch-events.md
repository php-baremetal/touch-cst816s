---
eyebrow: 'touch-cst816s · Recipe'
lede:    'Print the touches of a CST816S as they happen, in an event-driven project.'
see_also:
  - { href: '../usage/php-api.md', meta: 'PHP API' }
  - { href: 'package:php-baremetal/ext-input#docs/usage/events.md', meta: 'Events', label: 'Touch events' }
prev: { label: 'Cst816s', href: '../usage/php-api.md' }
next: { label: 'No next page', href: '#' }
---

# React to touches

Wire the chip's INT line (and reset, if you have it) to free GPIOs, and use the pin numbers of your board.

<!-- @code-block language="php" label="project-src/index.php" -->
```php
<?php
use Baremetal\Events;
use Baremetal\I2c\Bus;
use Baremetal\I2c\Driver\Cst816s;
use Baremetal\Input\{Pressed, Moved, Released, Gesture};

$touch = new Cst816s(new Bus(sda: 11, scl: 10), int: 4, rst: 1, width: 360, height: 360);

Events::listen(Pressed::class,  fn(Pressed $e)  => printf("down %d,%d\n", $e->x, $e->y));
Events::listen(Moved::class,    fn(Moved $e)    => printf("move %d,%d\n", $e->x, $e->y));
Events::listen(Released::class, fn(Released $e) => printf("up   %d,%d\n", $e->x, $e->y));
Events::listen(Gesture::class,  fn(Gesture $e)  => printf("gesture %s\n", $e->type->value));

$touch->events();
```
<!-- @endcode-block -->

Require `php-baremetal/touch-cst816s` and use the `event-driven` runtime. With a wrong orientation the points
land mirrored or swapped: set `mirror_x`, `mirror_y` or `swap_xy` until a touch in a corner reports that corner.
