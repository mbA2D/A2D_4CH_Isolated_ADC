# Selected STM32CubeF1 sources

Upstream: https://github.com/STMicroelectronics/STM32CubeF1, tag v1.8.7.
Exact superproject and submodule revisions are in `versions.json`.

The selected CMSIS headers/system/startup, HAL headers and required source
modules, and ST USB device core/CDC are unmodified upstream files. Their
component licenses are retained in each component directory. Application USB
glue under `hal/` is separate; `usbd_conf.c` is adapted from the STM3210E_EVAL
CDC_Standalone basic example. No Arduino core or PlatformIO package is vendored.
Only the listed HAL modules are compiled; unrelated vendor headers are inert.
