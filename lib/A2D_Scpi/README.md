# A2D_Scpi 1.0.0

A small C++11 library for line-based, SCPI-like instruments. It provides exact
command matching, argument splitting, query-marker rules, numeric conversion,
and bounded line framing. The library code uses fixed storage and the C/C++
standard library, with no dynamic allocation, Arduino/STM32 includes, or hardware
dependencies. Numeric conversion uses your toolchain's `strtof` implementation.
The included GPLv3 license is retained from the parent firmware repository.

## Use in another project

Copy this entire `A2D_Scpi` folder into the new PlatformIO project's `lib/`
directory. Include `A2D_Scpi.h`; PlatformIO discovers and builds the source.
No package installation or ADC firmware files are required.

For STM32CubeIDE or another C++ build, add `src/A2D_Scpi.cpp` to the build and
`src/` to its include paths. Enable C++11 or later. A C application can call it
through a small C++ adapter; the library itself exposes a C++ API.

Define your command table and handle the returned ID:

```cpp
#include <A2D_Scpi.h>

enum Command { SetOutput, ReadOutput };
const a2d_scpi::CommandSpec commands[] = {
    // Exact name, application ID, argument count, optional trailing '?'
    {"OUTP:STATE",  SetOutput,  1, false},
    {"OUTP:STATE?", ReadOutput, 0, false}
};

void handle(char *line) {
    a2d_scpi::ParsedCommand command;
    if (a2d_scpi::parse(line, commands, 2, command) !=
        a2d_scpi::ParseResult::Ready) return; // Project-specific error policy.
    if (command.spec->id == SetOutput) {
        uint32_t value;
        if (!a2d_scpi::unsignedInteger(command.arguments[0], 1, value)) return;
        // Apply value to this project's hardware only after validation.
    } else {
        // Send the state using this project's USB/UART/network output adapter.
    }
}
```

`examples/portable_instrument/` is a complete, independent relay-controller
example. Its application chooses its commands, validates ranges, owns its state,
and emits replies through a plain function callback. Build and run on a PC:

```text
g++ -std=c++11 -Isrc src/A2D_Scpi.cpp examples/portable_instrument/Instrument.cpp examples/portable_instrument/main.cpp -o instrument.exe
./instrument.exe "*IDN?" "OUTP:STATE 1" "OUTP:STATE?" "CONF:LIMIT 2.5"
```

The ADC project uses the same parser in `A2D_Protocol.cpp`. Its channel addressing,
calibration, configuration values and owned requests stay in that project.
`A2D_Dispatcher` handles its hardware and replies. Neither belongs in this library.

## Input and output adapters

`a2d_scpi::LineBuffer<N>` accepts bytes through `push()`. On `Complete`, consume
`data()` and call `reset()` before the next frame. On `TooLong`, it has already
discarded/reset the invalid frame. It strips one CR before LF and rejects embedded
NULs. Capacity includes the final NUL, so `LineBuffer<200>` accepts 199 bytes
before LF, including any CR. Keep a separate buffer for every input stream.

`a2d_scpi::LineReader<N>` wraps that buffer for ports providing:

```cpp
int available();
size_t readBytes(char *destination, size_t requested);
```

Those operations must not block for bytes reported as available, and `readBytes`
must return no more than requested. `poll(port, output)` consumes at most 64 bytes
per call and returns 1 for a complete line, 0 for pending input, or -1 for an
invalid frame. `output` must have N bytes. Extra coalesced lines remain cached;
keep polling, even when the underlying port reports empty. `has_pending_input()`
reports cached bytes; partial framing/discard state is retained internally.
`reset()` clears both pending bytes and partial framing.

For native HAL, DMA, sockets or interrupt-fed drivers, feed `LineBuffer` directly
or write the two-method adapter. Output, RS485 direction, response formatting,
timeouts and USB bootloader behavior are deliberately owned by the application.

## Parser contract and supported grammar

- `parse` takes a writable, NUL-terminated complete line, without its LF. It
  inserts NULs to split tokens. The caller controls the input-buffer size.
- `ParsedCommand` borrows the caller's buffer and command table. Consume its
  arguments before changing/destroying either. Copy validated values into your
  own request if dispatch will happen later. There is no hidden global cursor.
- Table names must be unique. Matching is case-sensitive and exact, including
  header question marks. IDs are application-owned 16-bit values.
- There are at most eight arguments, with an exact count per table entry. Spaces,
  tabs, CR and commas separate arguments; repeated separators are collapsed.
- A header query such as `OUTP:STATE?` is a separate literal table entry. Set
  `allowTrailingQuery=true` to additionally allow a single final `?` after the
  arguments, e.g. the ADC's `MEAS:VOLT 1?`. Its absence is also allowed. Unsupported
  or misplaced argument query markers fail before handlers run.
- `query` reports a literal header question mark or an accepted trailing marker.
- Results distinguish Ready, Empty, UnknownCommand and InvalidArguments. A failed
  parse leaves the result unchanged, but may modify the input buffer. A successful
  parse validates syntax/arity; application handlers must validate all argument
  types/ranges before mutating hardware.
- `unsignedInteger` accepts decimal digits with optional leading `+`, rejects
  overflow/negative values/trailing text, and checks an inclusive 32-bit maximum.
  `realNumber` uses `strtof`, rejects nonfinite/range errors and trailing text.
  Numeric outputs change only on success.

This implements a small explicit grammar. Abbreviations, case folding, quoted
strings, semicolon command lists, binary blocks, automatic error queues and full
SCPI/IEEE 488.2 compliance are not implemented. Add only what a project needs.

## Verification

Standalone library tests are included in this folder. From its root, run:

```text
g++ -std=c++11 -Wall -Wextra -Werror -pedantic -Isrc -Iexamples/portable_instrument src/A2D_Scpi.cpp examples/portable_instrument/Instrument.cpp tests/test_scpi.cpp -o test_scpi.exe
./test_scpi.exe
```

The ADC repository's `tools/run_tests.py` builds independent library tests without
any ADC or Arduino include path. It also copies this package to a temporary folder,
compiles the standalone example there, and checks its output. Existing ADC tests
continue to cover its commands, transports and settings independently.
