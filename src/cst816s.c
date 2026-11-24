/*
   +----------------------------------------------------------------------+
   | php-baremetal: touch-cst816s                                         |
   +----------------------------------------------------------------------+
   | Copyright (c) 2026 Gianfrancesco Aurecchia                           |
   +----------------------------------------------------------------------+
   | This source file is subject to the MIT license, that is bundled with |
   | this package in the file LICENSE.                                    |
   +----------------------------------------------------------------------+
   | Authors: Gianfrancesco Aurecchia <gianfri@php-baremetal.com>         |
   +----------------------------------------------------------------------+
*/

/* CST816S capacitive touch controller. Synchronous reads (point, points, gesture) and an interrupt-driven event
 * service: the INT line kicks irq.service on the executor, which reads the 6-byte report, tracks the finger and
 * raises Baremetal\Input events through ext-input. Uses only the neutral API of ext-i2c, ext-input and bm-common. */
#include <stdatomic.h>
#include <string.h>

#include "bm_i2c_driver.h"
#include "bm_i2c_hal.h"
#include "bm_input.h"
#include "bm_board.h"
#include "bm_gpio_hal.h"   /* ext-gpio's neutral GPIO HAL: the driver only drives the optional reset line */
#include "cst816s_track.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "ext/spl/spl_exceptions.h"

#define CST_REG_REPORT 0x01   /* GestureID, FingerNum, Xpos_H, Xpos_L, Ypos_H, Ypos_L */
#define CST_REG_MOTION 0xEC   /* MotionMask: bit0 EnDClick (double tap), bit1 EnConLR, bit2 EnConUD */
#define CST_DCLICK     0x01
#define CST_SAFETY_MS  50     /* re-read after a report with the finger down (see cst816s_service) */
#define CST_LATE_MS    350    /* with double tap on, the tap code comes ~260-270 ms after the release */

extern const i2c_driver_desc_t cst816s_driver_desc;

/* Per-device state, allocated zeroed with the device (state_size). cfg fields are written once by the
 * constructor, before anything can start the service. track belongs to the service (executor task) alone.
 * rec and irq are written by events() on the PHP task and read by the service. */
typedef struct {
    bool          inited;
    int           rst_pin;
    int           int_pin;
    bool          double_tap;      /* cfg: the wanted state of EnDClick */
    _Atomic bool  cfg_pending;     /* the chip has not taken the double tap setting yet (it was asleep) */
    cst_track_t   track;
    _Atomic(void *) rec;   /* the ext-input record; set BEFORE the service can start */
    _Atomic(void *) irq;   /* the HAL handle; set after bm_i2c_device_irq() returns, so an early edge sees NULL */
} cst_state;

static bm_err_t cst_read_report(i2c_dev_t *d, uint8_t b[6])
{
    return i2c_dev_read_reg(d, CST_REG_REPORT, b, 6);
}

/* Read-modify-write of bit0 (EnDClick) of MotionMask. The chip answers only while awake: after a reset, or while
 * touched. A failure leaves cfg_pending set and the next successful read in the service (or point() / gesture())
 * tries again. The flag is claimed with an exchange, so the PHP task and the executor never both write. */
static void cst_apply_config(i2c_dev_t *d, cst_state *st)
{
    if (!atomic_exchange_explicit(&st->cfg_pending, false, memory_order_acq_rel)) {
        return;
    }
    uint8_t m = 0;
    bm_err_t err = i2c_dev_read_reg(d, CST_REG_MOTION, &m, 1);
    if (err == BM_OK) {
        uint8_t want = st->double_tap ? (uint8_t) (m | CST_DCLICK) : (uint8_t) (m & ~CST_DCLICK);
        if (want != m) {
            err = i2c_dev_write_reg1(d, CST_REG_MOTION, want);
        }
    }
    if (err != BM_OK) {
        atomic_store_explicit(&st->cfg_pending, true, memory_order_release);
    }
}

static void cst_emit(void *rec, const cst_event_t *ev, int n)
{
    for (int i = 0; i < n; i++) {
        bm_input_touch_emit(rec, ev[i].kind, ev[i].x, ev[i].y, ev[i].gesture);
    }
}

/* irq.service, on the executor task (never touches PHP). Reads the report and emits what changed.
 * The handle may still be NULL when the first edge is served (bm_i2c_device_irq() has not returned yet): then
 * the safety read is not requested for that one run; the next edge or report requests it. */
static bm_err_t cst816s_service(i2c_dev_t *d)
{
    cst_state *st = bm_i2c_dev_state(d);
    void *rec = atomic_load_explicit(&st->rec, memory_order_acquire);
    void *irq = atomic_load_explicit(&st->irq, memory_order_acquire);
    uint8_t b[6];
    cst_event_t ev[2];
    int n;

    if (!rec) {
        return BM_OK;
    }
    bm_err_t err = cst_read_report(d, b);
    if (err != BM_OK) {
        /* The chip does not answer (asleep or off the bus). A finger that was down is gone as far as we can
         * tell: Released with the last point, so a consumer never waits for a release that cannot come. */
        n = cst_track_lost(&st->track, ev);
        cst_emit(rec, ev, n);
        return err;   /* logged by the HAL */
    }
    cst_apply_config(d, st);   /* the chip is awake now: take a double tap setting that was pending */
    n = cst_track_report(&st->track, b, ev);
    cst_emit(rec, ev, n);
    if (st->track.down && irq) {
        /* Safety read: if the chip gives no pulse at release, this read sees no finger and emits Released. */
        bm_i2c_hal_irq_after(irq, CST_SAFETY_MS);
    } else if (st->double_tap && irq && cst_track_late_wanted(&st->track)) {
        /* With double tap on the tap / double tap code arrives ~260 ms after the release, and it is not known
         * that INT pulses for it: look once more (a bounded number of times per release). */
        bm_i2c_hal_irq_after(irq, CST_LATE_MS);
    }
    return BM_OK;
}

static void cst816s_reset(const cst_state *st)
{
    if (st->rst_pin >= 0) {
        bm_gpio_hal_mode(st->rst_pin, 1);
        bm_gpio_hal_write(st->rst_pin, 0);
        bm_delay_ms(20);
        bm_gpio_hal_write(st->rst_pin, 1);
        bm_delay_ms(80);
    }
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_cst_ctor, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, bus, Baremetal\\I2c\\Bus, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, address, IS_LONG, 0, "0x15")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, int, IS_LONG, 0, "-1")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, rst, IS_LONG, 0, "-1")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, width, IS_LONG, 0, "240")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, height, IS_LONG, 0, "240")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, mirror_x, _IS_BOOL, 0, "false")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, mirror_y, _IS_BOOL, 0, "false")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, swap_xy, _IS_BOOL, 0, "false")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, double_tap, _IS_BOOL, 0, "true")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_cst_onboard, 0, 0, Baremetal\\I2c\\Driver\\Cst816s, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, block, IS_STRING, 0, "'touch'")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_cst_point, 0, 0, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_cst_points, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_cst_max_points, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_cst_gesture, 0, 0, Baremetal\\Input\\GestureType, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_cst_events, 0, 0, Baremetal\\Source, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(Cst816s, __construct)
{
    zval *zbus;
    zend_long addr = 0x15, intp = -1, rst = -1, width = 240, height = 240;
    bool mx = 0, my = 0, sw = 0, dt = 1;
    ZEND_PARSE_PARAMETERS_START(1, 10)
        Z_PARAM_OBJECT(zbus)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(addr)
        Z_PARAM_LONG(intp)
        Z_PARAM_LONG(rst)
        Z_PARAM_LONG(width)
        Z_PARAM_LONG(height)
        Z_PARAM_BOOL(mx)
        Z_PARAM_BOOL(my)
        Z_PARAM_BOOL(sw)
        Z_PARAM_BOOL(dt)
    ZEND_PARSE_PARAMETERS_END();
    if (width < 1 || width > 4096) {
        zend_argument_value_error(5, "must be between 1 and 4096");
        RETURN_THROWS();
    }
    if (height < 1 || height > 4096) {
        zend_argument_value_error(6, "must be between 1 and 4096");
        RETURN_THROWS();
    }

    /* The shared constructor checks the bus and the address and attaches the device. It reads (bus, address, hz)
     * from the first arguments of the call, so it is shown only those two. */
    uint32_t nargs = ZEND_CALL_NUM_ARGS(execute_data);
    if (nargs > 2) {
        ZEND_CALL_NUM_ARGS(execute_data) = 2;
    }
    i2c_driver_ctor(INTERNAL_FUNCTION_PARAM_PASSTHRU, &cst816s_driver_desc);
    ZEND_CALL_NUM_ARGS(execute_data) = nargs;
    if (EG(exception)) {
        RETURN_THROWS();
    }

    i2c_dev_t *d = bm_i2c_device_dev(Z_OBJ_P(ZEND_THIS));
    cst_state *st = d ? bm_i2c_dev_state(d) : NULL;
    if (!st) {
        zend_throw_exception(zend_ce_exception, "Cst816s: the device is not initialised", 0);
        RETURN_THROWS();
    }
    if (!st->inited) {   /* a second handle on the same device keeps the first configuration */
        st->rst_pin = (int) rst;
        st->int_pin = (int) intp;
        st->track.w = (int) width;
        st->track.h = (int) height;
        st->track.mirror_x = mx;
        st->track.mirror_y = my;
        st->track.swap_xy = sw;
        st->double_tap = dt;
        cst816s_reset(st);
        /* After a reset the chip is awake: write the setting now. Without a reset (or if it fails) the chip may
         * be asleep: the setting stays pending until a read succeeds. */
        atomic_store_explicit(&st->cfg_pending, true, memory_order_relaxed);
        cst_apply_config(d, st);
        st->inited = true;
    }
}

static bool board_int(const char *id, const char *block, const char *key, bool required, int def, int *out)
{
    if (bm_board_int(block, key, out)) {
        return true;
    }
    *out = def;
    if (required) {
        zend_throw_exception_ex(spl_ce_RuntimeException, 0, "board '%s': %s.%s is missing", id, block, key);
        return false;
    }
    return true;
}

/* onboard(string $block = 'touch'): the controller described by a block of the board, on the bus of its 'i2c'
 * block. Keys: int, rst (optional pins), width, height, address (optional), mirror-x, mirror-y, swap-xy (optional,
 * the names of the display block), double-tap (optional bool, default true). */
PHP_METHOD(Cst816s, onboard)
{
    zend_string *zblock = NULL;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_STR(zblock)
    ZEND_PARSE_PARAMETERS_END();
    const char *block = zblock ? ZSTR_VAL(zblock) : "touch";
    const char *id = bm_board_id();
    const char *type = bm_board_type(block);
    if (!type) {
        zend_throw_exception_ex(spl_ce_RuntimeException, 0, "board '%s' has no '%s' block", id, block);
        RETURN_THROWS();
    }
    if (strcmp(type, "cst816s") != 0) {
        zend_throw_exception_ex(spl_ce_RuntimeException, 0, "board '%s' has a %s of type '%s', not 'cst816s'", id,
                                block, type);
        RETURN_THROWS();
    }
    int addr, intp, rst, width, height, mx, my, sw, dt;
    if (!board_int(id, block, "width", true, 0, &width) || !board_int(id, block, "height", true, 0, &height)) {
        RETURN_THROWS();
    }
    board_int(id, block, "address", false, 0x15, &addr);
    board_int(id, block, "int", false, -1, &intp);
    board_int(id, block, "rst", false, -1, &rst);
    board_int(id, block, "mirror-x", false, 0, &mx);
    board_int(id, block, "mirror-y", false, 0, &my);
    board_int(id, block, "swap-xy", false, 0, &sw);
    board_int(id, block, "double-tap", false, 1, &dt);

    zend_string *bus_name = zend_string_init("Baremetal\\I2c\\Bus", sizeof("Baremetal\\I2c\\Bus") - 1, 0);
    zend_class_entry *bus_ce = zend_lookup_class(bus_name);
    zend_string_release(bus_name);
    if (!bus_ce) {
        zend_throw_exception(zend_ce_exception, "Cst816s: Baremetal\\I2c\\Bus is not available", 0);
        RETURN_THROWS();
    }
    zval bus, args[10];
    zend_call_method_with_0_params(NULL, bus_ce, NULL, "onboard", &bus);
    if (EG(exception)) {
        RETURN_THROWS();
    }

    zend_class_entry *ce = EX(func)->common.scope;
    object_init_ex(return_value, ce);
    ZVAL_COPY_VALUE(&args[0], &bus);
    ZVAL_LONG(&args[1], addr);
    ZVAL_LONG(&args[2], intp);
    ZVAL_LONG(&args[3], rst);
    ZVAL_LONG(&args[4], width);
    ZVAL_LONG(&args[5], height);
    ZVAL_BOOL(&args[6], mx != 0);
    ZVAL_BOOL(&args[7], my != 0);
    ZVAL_BOOL(&args[8], sw != 0);
    ZVAL_BOOL(&args[9], dt != 0);
    zend_call_known_instance_method(ce->constructor, Z_OBJ_P(return_value), NULL, 10, args);
    zval_ptr_dtor(&bus);
    if (EG(exception)) {
        zval_ptr_dtor(return_value);
        ZVAL_NULL(return_value);   /* the caller frees an unused result: not the object freed here */
        RETURN_THROWS();
    }
}

/* Reads the point touched now into *x, *y (screen coordinates): false with no finger or when the read fails (the
 * chip sleeps by itself and wakes on touch, so a read can fail). */
static bool cst_read_point(i2c_dev_t *d, int *x, int *y)
{
    cst_state *st = bm_i2c_dev_state(d);
    uint8_t b[6];
    bm_err_t rerr = cst_read_report(d, b);
    if (rerr == BM_OK) {
        cst_apply_config(d, st);   /* awake: take a pending double tap setting */
    }
    return rerr == BM_OK && cst_track_point(&st->track, b, x, y);
}

/* point(): the point touched now in screen coordinates, or null with no finger or when the read fails. */
PHP_METHOD(Cst816s, point)
{
    ZEND_PARSE_PARAMETERS_NONE();
    i2c_dev_t *d = i2c_device_this(ZEND_THIS);
    if (!d) {
        RETURN_THROWS();
    }
    int x, y;
    if (!cst_read_point(d, &x, &y)) {
        RETURN_NULL();
    }
    array_init_size(return_value, 2);
    add_assoc_long(return_value, "x", x);
    add_assoc_long(return_value, "y", y);
}

/* points(): the same read as point(), as the list of ext-input 1.1: empty, or the one finger with id 0. */
PHP_METHOD(Cst816s, points)
{
    ZEND_PARSE_PARAMETERS_NONE();
    i2c_dev_t *d = i2c_device_this(ZEND_THIS);
    if (!d) {
        RETURN_THROWS();
    }
    int x, y;
    if (!cst_read_point(d, &x, &y)) {
        RETURN_EMPTY_ARRAY();
    }
    zval p;
    array_init_size(&p, 3);
    add_assoc_long(&p, "id", 0);
    add_assoc_long(&p, "x", x);
    add_assoc_long(&p, "y", y);
    array_init_size(return_value, 1);
    add_next_index_zval(return_value, &p);
}

/* maxPoints(): the CST816S reports one finger. */
PHP_METHOD(Cst816s, maxPoints)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_LONG(1);
}

/* gesture(): the gesture the chip holds now (swipe directions in screen orientation), or null when there is none
 * or the read fails. */
PHP_METHOD(Cst816s, gesture)
{
    ZEND_PARSE_PARAMETERS_NONE();
    i2c_dev_t *d = i2c_device_this(ZEND_THIS);
    if (!d) {
        RETURN_THROWS();
    }
    cst_state *st = bm_i2c_dev_state(d);
    uint8_t code = 0;
    if (i2c_dev_read_reg(d, CST_REG_REPORT, &code, 1) != BM_OK) {
        RETURN_NULL();
    }
    cst_apply_config(d, st);   /* awake: take a pending double tap setting */
    bm_input_gesture_t g = cst_gesture_code(code);
    zend_object *c = g ? bm_input_gesture_case(cst_track_orient(&st->track, g)) : NULL;
    if (!c) {
        RETURN_NULL();
    }
    GC_ADDREF(c);   /* the case is borrowed */
    RETURN_OBJ(c);
}

/* events(): turns on the touch events; needs the INT pin. Idempotent: again returns a new Source handle on the
 * same record. The record goes into the state BEFORE the service starts (starting it hands over through the
 * executor queue, which synchronises); the HAL handle goes in after, and the service copes with NULL. */
PHP_METHOD(Cst816s, events)
{
    ZEND_PARSE_PARAMETERS_NONE();
    i2c_dev_t *d = i2c_device_this(ZEND_THIS);
    if (!d) {
        RETURN_THROWS();
    }
    cst_state *st = bm_i2c_dev_state(d);
    if (st->int_pin < 0) {
        zend_value_error("Cst816s::events(): needs the INT pin (int:)");
        RETURN_THROWS();
    }
    void *rec = NULL;
    zend_object *src = bm_input_touch_events(Z_OBJ_P(ZEND_THIS), &rec);
    if (!src) {
        RETURN_THROWS();
    }
    atomic_store_explicit(&st->rec, rec, memory_order_release);
    void *irq = bm_i2c_device_irq(Z_OBJ_P(ZEND_THIS), st->int_pin);
    if (!irq) {
        OBJ_RELEASE(src);
        RETURN_THROWS();
    }
    atomic_store_explicit(&st->irq, irq, memory_order_release);
    RETURN_OBJ(src);
}

static const zend_function_entry cst816s_methods[] = {
    PHP_ME(Cst816s, __construct, arginfo_cst_ctor,    ZEND_ACC_PUBLIC)
    PHP_ME(Cst816s, onboard,     arginfo_cst_onboard, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Cst816s, point,       arginfo_cst_point,   ZEND_ACC_PUBLIC)
    PHP_ME(Cst816s, points,      arginfo_cst_points,  ZEND_ACC_PUBLIC)
    PHP_ME(Cst816s, maxPoints,   arginfo_cst_max_points, ZEND_ACC_PUBLIC)
    PHP_ME(Cst816s, gesture,     arginfo_cst_gesture, ZEND_ACC_PUBLIC)
    PHP_ME(Cst816s, events,      arginfo_cst_events,  ZEND_ACC_PUBLIC)
    PHP_FE_END
};

const i2c_driver_desc_t cst816s_driver_desc = {
    .name = "cst816s",
    .php_class = "Cst816s",
    .capability = "Baremetal\\Input\\Touch",
    .default_addrs = { 0x15 },
    .n_default_addrs = 1,
    .default_hz = 400000,
    .state_size = sizeof(cst_state),
    .methods = cst816s_methods,
    /* The INT line (falling edge) kicks the service on the executor. */
    .irq = { .supported = true, .service = cst816s_service },
};
