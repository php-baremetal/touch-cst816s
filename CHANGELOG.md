# Changelog

## 1.1.0

- Added: `points()` (one finger, id 0) and `maxPoints()` (1) of ext-input 1.1. Requires php-baremetal/ext-input ^1.1.
- Fixed: a failed `Cst816s::onboard()` (no CST816S answering, for example) freed the new object twice, corrupting the PHP heap (`zend_mm_heap corrupted` at a later allocation, or at the next `onboard()`).

## 1.0.0

- First release.
