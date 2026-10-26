#define DT_DRV_COMPAT zmk_input_processor_scroll_latch

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <drivers/input_processor.h>

struct scroll_latch_config {
    uint32_t timeout_ms;
    bool invert_scroll_x;
    bool invert_scroll_y;
};

struct scroll_latch_data {
    bool latched;
    int64_t last_scroll_ms;
    int32_t pending_x;
};

static int scroll_latch_handle_event(const struct device *dev,
                                     struct input_event *event,
                                     uint32_t param1,
                                     uint32_t param2,
                                     struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);
    const struct scroll_latch_config *cfg = dev->config;
    struct scroll_latch_data *data = dev->data;

    // Any touch transition (down or up) marks a fresh gesture.
    if (event->type == INPUT_EV_KEY && event->code == INPUT_BTN_TOUCH) {
        data->latched = false;
        data->pending_x = 0;
        return ZMK_INPUT_PROC_CONTINUE;
    }
    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }
    if (event->code == INPUT_REL_WHEEL || event->code == INPUT_REL_HWHEEL) {
        data->latched = true;
        data->last_scroll_ms = k_uptime_get();
        return ZMK_INPUT_PROC_CONTINUE;
    }
    if (!data->latched) {
        return ZMK_INPUT_PROC_CONTINUE;
    }
    // Grace period over: the remaining finger goes back to moving the cursor.
    // A held-back X from a report straddling the timeout is dropped (one report's worth).
    if (k_uptime_get() - data->last_scroll_ms > cfg->timeout_ms) {
        data->latched = false;
        data->pending_x = 0;
        return ZMK_INPUT_PROC_CONTINUE;
    }

    // The trackpad driver reports REL_X (unsynced) then REL_Y (synced) for every
    // movement. Hold X back so the pair can be turned into a single scroll event
    // on the dominant axis, mirroring the driver's own scroll handling.
    if (event->code == INPUT_REL_X) {
        data->pending_x += event->value;
        return ZMK_INPUT_PROC_STOP;
    }
    if (event->code != INPUT_REL_Y) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    int32_t x = data->pending_x;
    int32_t y = event->value;
    data->pending_x = 0;

    if (abs(x) > abs(y)) {
        event->code = INPUT_REL_HWHEEL;
        event->value = cfg->invert_scroll_x ? -x : x;
    } else {
        event->code = INPUT_REL_WHEEL;
        event->value = cfg->invert_scroll_y ? -y : y;
    }
    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api scroll_latch_api = {
    .handle_event = scroll_latch_handle_event,
};

#define SCROLL_LATCH_INST(n)                                                   \
    static struct scroll_latch_data scroll_latch_data_##n;                    \
    static const struct scroll_latch_config scroll_latch_cfg_##n = {          \
        .timeout_ms = DT_INST_PROP(n, timeout_ms),                            \
        .invert_scroll_x = DT_INST_PROP(n, invert_scroll_x),                  \
        .invert_scroll_y = DT_INST_PROP(n, invert_scroll_y),                  \
    };                                                                         \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &scroll_latch_data_##n,              \
                          &scroll_latch_cfg_##n,                               \
                          POST_KERNEL,                                         \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                 \
                          &scroll_latch_api);

DT_INST_FOREACH_STATUS_OKAY(SCROLL_LATCH_INST)
