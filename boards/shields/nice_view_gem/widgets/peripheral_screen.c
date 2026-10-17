/*
 * Status screen for the left half when it is a split peripheral (dongle mode).
 * Only shows state the peripheral knows about: its own battery and the link to the central.
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#if defined(CONFIG_TOUCAN_STATUS_SCREEN) && CONFIG_TOUCAN_STATUS_SCREEN == 2
#include "battery_arc.h"
#else
#include "battery.h"
#endif

#include "screen.h"
#include "sleep.h"
#include "../assets/custom_fonts.h"

LV_IMG_DECLARE(toucan_logo);

struct connection_status_state {
    bool connected;
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/**
 * Draw buffers
 **/

static void draw_logo(lv_obj_t *canvas) {
    lv_draw_img_dsc_t img_dsc;
    lv_draw_img_dsc_init(&img_dsc);
    int x = (SCREEN_WIDTH - toucan_logo.header.w) / 2;
    lv_canvas_draw_img(canvas, x, 68, &toucan_logo, &img_dsc);
}

static void draw_connection_status(lv_obj_t *canvas, const struct status_state *state) {
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &quinquefive_8, LV_TEXT_ALIGN_RIGHT);
    lv_canvas_draw_text(canvas, -18, 143, SCREEN_WIDTH - 8, &label_dsc,
                        state->connected ? "BLE" : "NULL");
}

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);
    fill_background(canvas);

    if (is_sleep_screen_active()) {
        draw_sleep_screen(canvas);
        return;
    }

    // Draw widgets
    draw_battery_status(canvas, state);
    draw_logo(canvas);
    draw_connection_status(canvas, state);
}

/**
 * Battery status
 **/

static void set_battery_status(struct zmk_widget_screen *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state);

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

/**
 * Connection status
 **/

static void set_connection_status(struct zmk_widget_screen *widget,
                                  struct connection_status_state state) {
    widget->state.connected = state.connected;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void connection_status_update_cb(struct connection_status_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

static struct connection_status_state connection_status_get_state(const zmk_event_t *_eh) {
    return (struct connection_status_state){
        .connected = zmk_split_bt_peripheral_is_connected(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_connection_status, struct connection_status_state,
                            connection_status_update_cb, connection_status_get_state)
ZMK_SUBSCRIPTION(widget_connection_status, zmk_split_peripheral_status_changed);

/**
 * Activity state handling for sleep screen
 **/

static void force_redraw_all_widgets(void) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        draw_top(widget->obj, widget->cbuf, &widget->state);
    }
}

static int display_activity_event_handler(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return -ENOTSUP;
    }

    switch (ev->state) {
    case ZMK_ACTIVITY_ACTIVE:
        set_sleep_screen_active(false);
        break;
    case ZMK_ACTIVITY_SLEEP:
        set_sleep_screen_active(true);
        force_redraw_all_widgets();
        // Force LVGL to process pending updates and flush to display hardware
        // before the CPU enters deep sleep
        lv_task_handler();
        lv_refr_now(NULL);
        break;
    default:
        break; // ignore other states (like IDLE)
    }
    return 0;
}

ZMK_LISTENER(nice_view_gem_display, display_activity_event_handler);
ZMK_SUBSCRIPTION(nice_view_gem_display, zmk_activity_state_changed);

/**
 * Initialization
 **/

int zmk_widget_screen_init(struct zmk_widget_screen *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, SCREEN_WIDTH, SCREEN_HEIGHT);

    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, SCREEN_WIDTH, SCREEN_HEIGHT, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_connection_status_init();

    return 0;
}

lv_obj_t *zmk_widget_screen_obj(struct zmk_widget_screen *widget) { return widget->obj; }
