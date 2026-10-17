# Mouse Acceleration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add velocity-based, precision-first mouse acceleration to the Toucan's glidepoint, replacing the current fixed `zip_xy_scaler 150 100` with a custom input processor that scales slowly-moved deltas at 1.0× and fast-moved deltas at 2.5×.

**Architecture:** A new C input processor (`zmk,input-processor-accel`) lives in the toucan shield directory. For each `INPUT_EV_REL` X/Y event, it reads the delta magnitude as a proxy for speed and applies a linearly-interpolated scale factor between `min-scale` and `max-scale`. All arithmetic is integer-only (no floats) for battery efficiency. The processor is wired into `glidepoint_listener` in place of `zip_xy_scaler`.

**Tech Stack:** ZMK v0.3 input processor API (`drivers/input_processor.h`), Zephyr devicetree bindings (YAML), C99.

---

## File Map

| File | Action |
|------|--------|
| `zephyr/module.yml` | Modify — add `dts_root: .` so `dts/bindings/` is searched |
| `dts/bindings/input/zmk,input-processor-accel.yaml` | Create — devicetree binding |
| `boards/shields/toucan/CMakeLists.txt` | Create — adds C source to app build |
| `boards/shields/toucan/src/input_processor_accel.c` | Create — accelerator logic |
| `boards/shields/toucan/toucan.dtsi` | Modify — add `zip_accel` node, swap into `glidepoint_listener` |

---

### Task 1: Add `dts_root` to module.yml

The `dts_root: .` setting tells Zephyr to search `./dts/bindings/` in this module for devicetree binding YAML files. Without it, the new binding won't be found and the build will fail with "unknown compatible".

**Files:** Modify `zephyr/module.yml`

- [ ] Open `zephyr/module.yml`. Current content:
  ```yaml
  build:
    settings:
      board_root: .
  ```

- [ ] Add `dts_root: .` under `settings`:
  ```yaml
  build:
    settings:
      board_root: .
      dts_root: .
  ```

- [ ] Commit:
  ```bash
  git add zephyr/module.yml
  git commit -m "build: add dts_root to expose dts/bindings/ to Zephyr"
  ```

---

### Task 2: Create the devicetree binding

This YAML file tells Zephyr's DT compiler what properties the `zmk,input-processor-accel` compatible node accepts.

**Files:** Create `dts/bindings/input/zmk,input-processor-accel.yaml`

- [ ] Create the directory: `mkdir -p dts/bindings/input/`

- [ ] Write the file:
  ```yaml
  description: Velocity-based mouse acceleration input processor

  compatible: "zmk,input-processor-accel"

  include: base.yaml

  properties:
    "#input-processor-cells":
      type: int
      const: 0
      required: true

    min-delta:
      type: int
      required: true
      description: >
        Event delta magnitude (|value|) at or below which min-scale applies.
        Represents a slow swipe. Tune between 1–5.

    max-delta:
      type: int
      required: true
      description: >
        Event delta magnitude at or above which max-scale applies.
        Represents a fast swipe. Tune between 8–20.

    min-scale:
      type: int
      required: true
      description: >
        Scale numerator for slow movement; denominator is 100.
        100 = 1.0x, 150 = 1.5x.

    max-scale:
      type: int
      required: true
      description: >
        Scale numerator for fast movement; denominator is 100.
        250 = 2.5x, 300 = 3.0x.
  ```

- [ ] Commit:
  ```bash
  git add dts/bindings/input/zmk,input-processor-accel.yaml
  git commit -m "feat: add zmk,input-processor-accel devicetree binding"
  ```

---

### Task 3: Add CMakeLists.txt to the toucan shield

The toucan shield currently has no CMakeLists.txt (unlike `toucan_pet`). Adding one lets ZMK include the accelerator C file in the firmware build.

**Files:** Create `boards/shields/toucan/CMakeLists.txt`

- [ ] Write the file:
  ```cmake
  target_sources(app PRIVATE
      src/input_processor_accel.c
  )
  ```

- [ ] Commit:
  ```bash
  git add boards/shields/toucan/CMakeLists.txt
  git commit -m "build: add CMakeLists.txt to toucan shield"
  ```

---

### Task 4: Implement the accelerator input processor

**Files:** Create `boards/shields/toucan/src/input_processor_accel.c`

The processor intercepts `INPUT_EV_REL` X/Y events. For each, it computes `magnitude = |value|`, then linearly interpolates a scale factor between `min_scale` and `max_scale` based on whether magnitude falls below `min_delta`, above `max_delta`, or between them. The scaled value replaces the event's original value.

- [ ] Create directory: `mkdir -p boards/shields/toucan/src/`

- [ ] Write the file:
  ```c
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
      const struct accel_config *cfg = dev->config;

      if (event->type != INPUT_EV_REL) {
          return ZMK_INPUT_PROC_CONTINUE;
      }
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

  #define ACCEL_INST(n)                                                  \
      static const struct accel_config accel_cfg_##n = {                \
          .min_delta = DT_INST_PROP(n, min_delta),                      \
          .max_delta = DT_INST_PROP(n, max_delta),                      \
          .min_scale = DT_INST_PROP(n, min_scale),                      \
          .max_scale = DT_INST_PROP(n, max_scale),                      \
      };                                                                 \
      DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &accel_cfg_##n,        \
                            POST_KERNEL,                                 \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,         \
                            &accel_api);

  DT_INST_FOREACH_STATUS_OKAY(ACCEL_INST)
  ```

- [ ] Commit:
  ```bash
  git add boards/shields/toucan/src/input_processor_accel.c
  git commit -m "feat: implement zmk,input-processor-accel input processor"
  ```

---

### Task 5: Wire the accelerator into toucan.dtsi

Replace `zip_xy_scaler 150 100` with `&zip_accel` and add the `zip_accel` devicetree node. The scroller block is unchanged.

**Files:** Modify `boards/shields/toucan/toucan.dtsi`

- [ ] Replace the second `/ { ... }` block (lines 56–82) with:
  ```dts
  / {
      split_inputs {
          #address-cells = <1>;
          #size-cells = <0>;

          glidepoint_split: glidepoint_split@0 {
              compatible = "zmk,input-split";
              reg = <0>;
          };
      };

      zip_accel: zip_accel {
          compatible = "zmk,input-processor-accel";
          #input-processor-cells = <0>;
          min-delta = <2>;
          max-delta = <12>;
          min-scale = <100>;
          max-scale = <250>;
      };

      glidepoint_listener: glidepoint_listener {
          compatible = "zmk,input-listener";
          status = "disabled";
          device = <&glidepoint_split>;
          input-processors = <&zip_accel>;
          scroller {
              layers = <1 2>;
              input-processors = <
                  &zip_xy_to_scroll_mapper
                  &zip_scroll_snap
                  &zip_scroll_scaler 1 10
                  &zip_scroll_transform INPUT_TRANSFORM_Y_INVERT
              >;
          };
      };
  };
  ```

- [ ] Commit:
  ```bash
  git add boards/shields/toucan/toucan.dtsi
  git commit -m "feat: replace zip_xy_scaler with zip_accel in glidepoint_listener"
  ```

---

### Task 6: Build and fix any compilation errors

- [ ] Push the branch and let the GitHub Actions CI build run, or build locally:
  ```bash
  west build -d build/toucan_left -b nice_nano_v2 \
    -- -DSHIELD="toucan_left toucan" \
       -DZMK_CONFIG=$(pwd)/config
  ```
  Expected: build completes with no errors.

- [ ] **If build fails: "unknown compatible zmk,input-processor-accel"**
  → `dts_root` wasn't picked up. Verify `zephyr/module.yml` has `dts_root: .` and the binding is at exactly `dts/bindings/input/zmk,input-processor-accel.yaml` from the repo root.

- [ ] **If build fails: "no such file drivers/input_processor.h"**
  → Find the correct header by running:
  ```bash
  find ~/.west/zmk -name "input_processor.h" 2>/dev/null
  ```
  Replace the include path in `input_processor_accel.c` with the path found (relative to the ZMK include root).

- [ ] **If build fails: "ZMK_INPUT_PROC_CONTINUE undeclared"**
  → Some ZMK v0.3 builds use the integer literal `0` instead of the macro. Replace both `return ZMK_INPUT_PROC_CONTINUE;` lines with `return 0;`.

- [ ] **If build fails: "zmk_input_processor_state undeclared"**
  → The state struct may be named differently. Check the ZMK source:
  ```bash
  grep -r "zmk_input_processor" ~/.west/zmk/app/include/ 2>/dev/null | head -20
  ```
  Update the function signature to match.

---

### Task 7: Flash and tune the feel

After a successful build, flash and verify the feel on the trackpad.

- [ ] Slow swipes (deliberate, precise movements) should feel smooth at 1.0× speed.
- [ ] Fast swipes should noticeably accelerate and reach ~2.5× speed.

**Tuning knobs in `toucan.dtsi`** (rebuild and reflash after each change):

| Property | Current | Effect of increasing |
|----------|---------|---------------------|
| `min-scale` | 100 (1.0×) | Faster baseline at slow speed — raise to 130–150 if overall feel is too slow |
| `max-scale` | 250 (2.5×) | More speed boost on fast swipes — raise to 300 for more aggressive feel |
| `min-delta` | 2 | Larger precision zone — lower = more events treated as "slow" |
| `max-delta` | 12 | Acceleration starts sooner — lower to 8 if you want boost to kick in earlier |

Typical first-pass adjustments:
- Cursor too slow overall → raise `min-scale` to 140
- Fast swipes not boosted enough → raise `max-scale` to 300 or lower `max-delta` to 8
- Precision feel too aggressive → raise `min-delta` to 3
