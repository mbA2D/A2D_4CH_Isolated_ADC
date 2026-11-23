"""Compile production modules as C11 and run host regressions without hardware."""
import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++", help="C++ compiler for the Windows test harnesses")
    parser.add_argument("--c-compiler", help="C compiler; defaults to gcc beside the C++ compiler")
    args = parser.parse_args()
    cxx = shutil.which(args.compiler)
    if not cxx:
        raise SystemExit("Install a host C/C++ toolchain or pass --compiler <path>")
    cc = shutil.which(args.c_compiler) if args.c_compiler else str(Path(cxx).with_name(Path(cxx).name.replace("g++", "gcc")))
    if not cc or not Path(cc).is_file():
        raise SystemExit("Pass --c-compiler <gcc path>")
    root = Path(__file__).resolve().parents[1]
    build = root / "build" / "host-tests"
    scpi = root / "lib" / "A2D_Scpi"
    core = [scpi / "src" / "A2D_Scpi.c", scpi / "src" / "A2D_ScpiLineReader.c", scpi / "src" / "A2D_ScpiErrorQueue.c"]
    suites = (
        ("test_scpi", scpi / "tests" / "test_scpi.c", None, core + [scpi / "examples" / "portable_instrument" / "Instrument.c"]),
        ("test_firmware", root / "tests/test_firmware.cpp", "fakes", core + [root / ("A2D_" + module + ".c") for module in
            ("IO", "Protocol", "Transport", "Dispatcher", "Application", "Calibration")] + [root / "tests/fakes/A2D_4CH_Isolated_ADC.c"]),
        ("test_usb_reset", root / "tests/test_usb_reset.c", None, [root / "A2D_USB_Bootloader.c"]),
        ("test_usb_session", root / "tests/test_usb_session.c", "session_fakes", []),
        ("test_uart", root / "tests/test_uart.c", "uart_fakes", [scpi / "src/A2D_ScpiLineReader.c"]),
        ("test_services", root / "tests/test_services.cpp", "service_fakes", [root / ("A2D_" + module + ".c") for module in
            ("Calibration", "SettingsStore", "Measurement", "4CH_Isolated_ADC", "Flash")]),
        ("test_io", root / "tests/test_io.c", None, [root / "A2D_IO.c"]),
    )
    # Match the target's binary32/binary64 behavior, avoiding x87 excess precision.
    flags = ["-Wall", "-Wextra", "-Werror", "-pedantic", "-msse2", "-mfpmath=sse"]
    for name, test, fakes, sources in suites:
        directory = build / name
        directory.mkdir(parents=True, exist_ok=True)
        includes = ["-I", str(scpi / "src"), "-I", str(scpi / "examples/portable_instrument")]
        if fakes:
            includes += ["-I", str(root / "tests" / fakes)]
        includes += ["-I", str(root)]
        objects = []
        for index, source in enumerate(sources):
            output = directory / (str(index) + "_" + source.stem + ".o")
            subprocess.run([cc, "-std=c11", *flags, *includes, "-c", str(source), "-o", str(output)], check=True)
            objects.append(str(output))
        executable = directory / (name + ".exe")
        compiler, standard = (cxx, "-std=c++11") if test.suffix == ".cpp" else (cc, "-std=c11")
        subprocess.run([compiler, standard, *flags, *includes, str(test), *objects, "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=30)

    # Build a copied distributable using only a C compiler and its own files.
    with tempfile.TemporaryDirectory(prefix="a2d-scpi-c-") as temporary:
        package = Path(temporary) / "A2D_Scpi"
        shutil.copytree(scpi, package)
        example = package / "examples/portable_instrument"
        executable = Path(temporary) / "instrument.exe"
        subprocess.run([cc, "-std=c11", *flags, "-I", str(package / "src"),
            str(package / "src/A2D_Scpi.c"), str(package / "src/A2D_ScpiLineReader.c"), str(package / "src/A2D_ScpiErrorQueue.c"),
            str(example / "Instrument.c"), str(example / "main.c"), "-o", str(executable)], check=True)
        completed = subprocess.run([str(executable), "*IDN?", "OUTP:STATE 1", "OUTP:STATE?", "CONF:LIMIT 2.5", "OUTP:STATE 0?", "OUTP:STATE?", "SYST:ERR?", "SYST:ERR?"],
            check=True, capture_output=True, text=True, timeout=5)
        if completed.stdout != 'Example,Relay,DEMO,1.0\n1\n1\n-102,"Syntax error"\n0,"No error"\n':
            raise RuntimeError(f"Standalone C example returned {completed.stdout!r}")
        print("PASS copied-package standalone C example (no ADC/HAL/C++ dependency)", flush=True)


if __name__ == "__main__":
    main()
