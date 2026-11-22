"""Build identity and target-checked USB updates for the Maple v2 bootloader."""

import re
import subprocess
import sys
from pathlib import Path

Import("env")
env.Replace(PROGNAME="firmware")

# Patch an environment-local copy, never the installed/shared framework.
sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "tools"))
from usb_rx_patch import patch_cdc_source


def patch_usb_cdc(build_env, node):
    source = Path(node.srcnode().get_abspath())
    if source.name != "usbd_cdc.c":
        return node
    patched = Path(build_env.subst("$BUILD_DIR")) / "patched-framework" / "usbd_cdc.c"
    patched.parent.mkdir(parents=True, exist_ok=True)
    content = patch_cdc_source(source.read_bytes())
    if not patched.exists() or patched.read_bytes() != content:
        patched.write_bytes(content)
    return build_env.File(str(patched))


env.AddBuildMiddleware(patch_usb_cdc, "*usbd_cdc.c")

device_serial = env.GetProjectOption("custom_device_serial")
if not re.fullmatch(r"[0-9]{4}", device_serial):
    raise ValueError("custom_device_serial must contain exactly four digits")
env.Append(CPPDEFINES=[("A2D_DEVICE_SERIAL", env.StringifyMacro(device_serial))])
env.BoardConfig().update("build.hwids", [["0x0483", "0x5740"]])


def check_image(source, target, env):
    binary = Path(str(target[0]))
    if binary.stat().st_size > 54 * 1024:
        raise ValueError("Application exceeds 54 KiB; bootloader/calibration space is reserved")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_image)

if env.GetProjectOption("upload_protocol") == "custom":
    command = [
        env.subst("$PYTHONEXE"),
        str(Path(env.subst("$PROJECT_DIR")) / "tools" / "usb_upload.py"),
        "--port", env.subst("$UPLOAD_PORT") or "UNSET",
        "--serial", device_serial,
        "--version", re.search(r'#define VERSION \("([^"]+)"\)', (Path(env.subst("$PROJECT_DIR")) / "A2D_4CH_Isolated_ADC_V1.0.h").read_text()).group(1),
        "--dfu-util", str(Path(env.PioPlatform().get_package_dir("tool-dfuutil")) / "bin" / "dfu-util.exe"),
        "--maple-dfu-util", str(Path(env.PioPlatform().get_package_dir("tool-stm32duino")) / "dfu-util.exe"),
        "--binary", env.subst("$BUILD_DIR/${PROGNAME}.bin"),
    ]
    usb_serial = env.GetProjectOption("custom_usb_serial", "")
    if usb_serial:
        command.extend(["--usb-serial", usb_serial])
    if env.GetProjectOption("custom_allow_settings_reset", "no").lower() == "yes":
        command.append("--allow-settings-reset")
    env.Replace(UPLOADCMD=subprocess.list2cmdline(command))
