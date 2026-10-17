#define DT_DRV_COMPAT zmk_input_processor_accel

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <drivers/input_processor.h>

struct accel_config {
    uint32_t min_delta;
    uint32_t max_delta;
    uint32_t min_scale;
    uint32_t max_scale;
};

static int accel_handle_event(const struct device *dev,
                               struct input_event *event,
                               uint32_t param1,
                               uint32_t param2,
                               struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);
    const struct accel_config *cfg = dev->config;

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }
    // Scroll wheel codes are handled by the separate scroller sub-node pipeline.
    // Speed is approximated per-axis: diagonal swipes scale each axis independently,
    // so a diagonal at speed S applies the same scale as an axial swipe at speed S/sqrt(2).
    if (event->code != INPUT_REL_X && event->code != INPUT_REL_Y) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    int32_t val = event->value;
    uint32_t magnitude = (val < 0) ? (uint32_t)(-val) : (uint32_t)val;
    uint32_t scale;

    if (magnitude <= cfg->min_delta) {
        scale = cfg->min_scale;
    } else if (magnitude >= cfg->max_delta) {
        scale = cfg->max_scale;
    } else {
        uint32_t t = magnitude - cfg->min_delta;
        uint32_t range = cfg->max_delta - cfg->min_delta;
        scale = cfg->min_scale + (cfg->max_scale - cfg->min_scale) * t / range;
    }

    event->value = val * (int32_t)scale / 100;
    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api accel_api = {
    .handle_event = accel_handle_event,
};

#define ACCEL_INST(n)                                                          \
    static const struct accel_config accel_cfg_##n = {                        \
        .min_delta = DT_INST_PROP(n, min_delta),                              \
        .max_delta = DT_INST_PROP(n, max_delta),                              \
        .min_scale = DT_INST_PROP(n, min_scale),                              \
        .max_scale = DT_INST_PROP(n, max_scale),                              \
    };                                                                         \
    BUILD_ASSERT(DT_INST_PROP(n, max_delta) > DT_INST_PROP(n, min_delta),    \
                 "max-delta must be greater than min-delta");                  \
    BUILD_ASSERT(DT_INST_PROP(n, max_scale) >= DT_INST_PROP(n, min_scale),   \
                 "max-scale must be >= min-scale");                            \
    BUILD_ASSERT(DT_INST_PROP(n, max_scale) <= 2000,                          \
                 "max-scale must be <= 2000 (prevents overflow)");             \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &accel_cfg_##n,                \
                          POST_KERNEL,                                         \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                 \
                          &accel_api);

DT_INST_FOREACH_STATUS_OKAY(ACCEL_INST)
