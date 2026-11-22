"""Compile and run the firmware regression tests on the host, without hardware."""

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if compiler is None:
        raise SystemExit("Install a host C++ compiler or pass --compiler <path>")
    root = Path(__file__).resolve().parents[1]
    build = root / ".pio" / "host-tests"
    build.mkdir(parents=True, exist_ok=True)
    scpi = root / "lib" / "A2D_Scpi"
    core = scpi / "src" / "A2D_Scpi.cpp"
    suites = (
        ("test_scpi", None, [core, scpi / "examples" / "portable_instrument" / "Instrument.cpp"]),
        ("test_firmware", "fakes", [core] + [root / ("A2D_" + module + ".cpp") for module in
                                             ("Protocol", "Transport", "Dispatcher", "Application", "Calibration")]),
        ("test_usb_reset", "reset_fakes", []),
        ("test_usb_session", "session_fakes", []),
        ("test_services", "service_fakes", [root / ("A2D_" + module + ".cpp") for module in
                                           ("Calibration", "SettingsStore", "Measurement", "4CH_Isolated_ADC", "Flash")]),
    )
    for name, fakes, sources in suites:
        executable = build / (name + ".exe")
        test = scpi / "tests" / "test_scpi.cpp" if name == "test_scpi" else root / "tests" / (name + ".cpp")
        includes = ["-I", str(scpi / "src"), "-I", str(scpi / "examples" / "portable_instrument")]
        if fakes:
            includes += ["-I", str(root / "tests" / fakes), "-I", str(root)]
        subprocess.run([
            compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            *includes,
            str(test),
            *map(str, sources),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True, timeout=15)

    # Prove the distributable folder works without any file from the ADC project.
    with tempfile.TemporaryDirectory(prefix="a2d-scpi-") as temporary:
        package = Path(temporary) / "A2D_Scpi"
        shutil.copytree(scpi, package)
        example = package / "examples" / "portable_instrument"
        executable = Path(temporary) / "instrument.exe"
        subprocess.run([
            compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(package / "src"), str(package / "src" / "A2D_Scpi.cpp"),
            str(example / "Instrument.cpp"), str(example / "main.cpp"),
            "-o", str(executable),
        ], check=True)
        completed = subprocess.run([
            str(executable), "*IDN?", "OUTP:STATE 1", "OUTP:STATE?", "CONF:LIMIT 2.5",
            "OUTP:STATE 0?", "OUTP:STATE?",
        ], check=True, capture_output=True, text=True, timeout=5)
        if completed.stdout != "Example,Relay,DEMO,1.0\n1\nERROR:COMMAND\n1\n":
            raise RuntimeError(f"Standalone example returned {completed.stdout!r}")
        print("PASS copied-package standalone example (no ADC project files)", flush=True)


if __name__ == "__main__":
    main()
