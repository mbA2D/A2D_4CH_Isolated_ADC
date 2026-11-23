# A2D_Scpi 3.1.0

A small C11 library for line-based, SCPI-like instruments. It provides exact
command matching, argument splitting, query-marker rules, numeric conversion,
bounded line framing, and an optional fixed-size error queue. All parser, reader and example state belongs to the
caller. The library has no ADC, HAL, STM32, Arduino or C++ dependency and calls
no allocation functions. Numeric conversion delegates to the toolchain's
`strtof`; its supported syntax, locale, range errors and internal implementation
remain libc-dependent. The GPLv3 license is retained from the parent repository.

Version 3.1.0 adds `A2D_ScpiErrorQueue` and updates the example to queue errors
and answer only queries. Version 3.0.0 requires query markers on headers and commas between arguments.
It removes `allow_trailing_query` from `A2D_ScpiCommandSpec`; table entries now
have three fields. The C11 API and C++ linkage guards are retained. This is a
breaking grammar/API update from 2.0.0.

## Use in another project

Copy this entire folder to the new project's library directory. Compile
`src/A2D_Scpi.c`, `src/A2D_ScpiLineReader.c` and `src/A2D_ScpiErrorQueue.c`
as C11 and add `src/` to the
include path. No platform framework or ADC files are required. PlatformIO can
use the included `library.json`; STM32CubeIDE, CMake and ordinary C compilers
can compile these source files directly. The error queue source is optional
when your project does not use it.

Define your own command table and validate values before applying changes:

```c
#include <A2D_Scpi.h>

enum Command { SetOutput, ReadOutput };
static const A2D_ScpiCommandSpec commands[] = {
    {"OUTP:STATE",  SetOutput,  1},
    {"OUTP:STATE?", ReadOutput, 0}
};

void handle(char *line) {
    A2D_ScpiParsedCommand command;
    if (a2d_scpi_parse(line, commands, 2, &command) != A2D_SCPI_READY) return;
    if (command.spec->id == SetOutput) {
        uint32_t value;
        if (!a2d_scpi_unsigned_integer(command.arguments[0], 1, &value)) return;
        /* Apply value to this project's hardware after validation. */
    } else {
        /* Reply using this project's output adapter. */
    }
}
```

`examples/portable_instrument/` is an independent relay-controller example. It
owns its state in `Instrument`, initializes it with `instrument_init`, validates
commands in `instrument_execute`, and replies through a callback plus context.
The reply callback consumes a NUL-terminated string synchronously. Output and
hardware policy belong to the example, not to the parser library.

From this folder, with GCC on PATH:

```text
gcc -std=c11 -Wall -Wextra -Werror -pedantic -Isrc -Iexamples/portable_instrument src/A2D_Scpi.c src/A2D_ScpiLineReader.c src/A2D_ScpiErrorQueue.c examples/portable_instrument/Instrument.c examples/portable_instrument/main.c -o instrument.exe
./instrument.exe "*IDN?" "OUTP:STATE 1" "OUTP:STATE?" "CONF:LIMIT 2.5" "OUTP:STATE 0?" "OUTP:STATE?" "SYST:ERR?" "SYST:ERR?"
```

The command prints these lines; the unsupported setter query leaves the output
state at 1:

```text
Example,Relay,DEMO,1.0
1
1
-102,"Syntax error"
0,"No error"
```

## Line buffers and input callbacks

Initialize a separate reader and backing array for each input stream:

```c
#include <A2D_ScpiLineReader.h>

static A2D_ScpiLineReader reader;
static char storage[200];

/* Supply these functions using your port implementation. */
int port_available(void *context);
size_t port_read(void *context, char *destination, size_t requested);

bool start_reader(void) {
    return a2d_scpi_reader_init(&reader, storage, sizeof(storage));
}

int read_command(void *port_context, char output[200]) {
    const A2D_ScpiInput input = {port_context, port_available, port_read};
    return a2d_scpi_reader_poll(&reader, &input, output);
}
```

The backing array must outlive its initialized buffer/reader and must not be
shared between active readers. Capacity includes the final NUL and must be at
least two. Initialization returns false for a NULL object/storage or a smaller
capacity without changing the object or storage. Reset after initialization
before starting a new transport session. The public state fields are maintained
by the library; do not change them or use an uninitialized object.

For byte-fed HAL/DMA/interrupt adapters, `a2d_scpi_line_init`,
`a2d_scpi_line_push` and `a2d_scpi_line_reset` expose the same framing directly.

- `A2D_SCPI_LINE_PENDING`: continue supplying bytes.
- `A2D_SCPI_LINE_COMPLETE`: consume `line.data`, then call
  `a2d_scpi_line_reset` before the next frame. Completion does not reset a direct
  line buffer automatically.
- `A2D_SCPI_LINE_TOO_LONG`: the invalid frame has been discarded/reset at LF.

LF completes a frame; one preceding CR is stripped. Embedded NULs and overlong
frames are discarded through the next LF, producing one rejection rather than
an executable suffix. `capacity=200` accepts 199 bytes before LF, including an
optional CR. The next frame starts clean after rejection.

The input callbacks must be nonblocking for bytes reported available, and `read`
must write/return no more than requested. Zero and short reads are supported.
The callback context is passed through unchanged and may be NULL if the callbacks
support it. Callback descriptors need only live for the duration of `poll`;
callbacks are not retained. Feed each reader consistently from one input stream.

`a2d_scpi_reader_poll` returns 1 for a complete line, 0 for pending input, or -1
for an invalid frame. It also returns -1 for NULL required pointers/callbacks or
an uninitialized zeroed reader. Its output must have at least the initialized
capacity bytes and must not overlap reader state, its backing array, or input
storage. Output changes only when a complete line is returned. The reader
copies completed text to output and resets its internal line buffer.

Each call processes at most `A2D_SCPI_LINE_BUDGET` (64) bytes. Fragmented input
and extra coalesced lines are retained. Keep polling cached input even if the
port now reports empty. `a2d_scpi_reader_has_pending` reports cached unread bytes,
not a merely unfinished frame or overflow-discard state.
`a2d_scpi_reader_reset` clears both cached bytes and partial/discard state; it
does not drain bytes that are still in the underlying port.

Invalid use of an uninitialized/zeroed line buffer returns TOO_LONG from push;
valid callers must initialize first. Reset tolerates NULL. For all initialized
objects, preserve the backing pointer and capacity for their lifetime.

## Parser contract and grammar

- `a2d_scpi_parse` requires a writable, NUL-terminated complete line without its
  LF. It inserts NULs while splitting tokens. The caller guarantees storage and
  termination; this API does not accept an explicit input length.
- The result borrows the caller's line and command table. Consume its tokens
  before changing/destroying either. Copy validated values into your own owned
  request if dispatch happens later. There is no global token cursor.
- Table names are non-NULL, unique, case-sensitive exact matches, including
  header question marks. IDs are application-owned uint16_t values.
- Command table entries declare an exact argument count of at most
  `A2D_SCPI_MAX_ARGUMENTS` (eight). Space, tab and CR delimit the header; each
  subsequent argument must be separated by one comma. Surrounding whitespace
  is allowed, but whitespace inside a token is rejected. Empty fields and
  leading/trailing/repeated commas are rejected. A comma does not delimit a header.
- Queries use a separate literal table entry ending in `?`, such as `READ?`.
  `READ? 7` is valid for a one-argument query; `READ 7?`, `READ 7` and `READ?7`
  do not match it. Question marks in parameters are rejected.
- `query` reports whether the matched header ends in `?`.
- One complete command per line is supported. Any semicolon or embedded LF
  rejects the entire input before matching a command prefix. Framing still
  accepts consecutive LF-terminated commands as separate lines.
- Results distinguish `A2D_SCPI_READY`, `A2D_SCPI_EMPTY`,
  `A2D_SCPI_UNKNOWN_COMMAND` and `A2D_SCPI_INVALID_ARGUMENTS`. The result is
  published only on READY. Failure can modify the input but leaves the result
  unchanged. NULL line/result or a NULL table with nonzero count returns
  UNKNOWN_COMMAND. A NULL table with zero count is permitted.
- Successful parsing validates syntax/arity. Your handler must validate every
  argument's type/range before changing hardware or routing a request.
- `a2d_scpi_unsigned_integer` accepts decimal digits with optional leading `+`,
  rejects negative/fractional/overflowing input and trailing characters, and
  checks an inclusive uint32_t maximum before multiplication/addition.
- `a2d_scpi_real_number` preserves `strtof` syntax, including decimal exponents
  and hex floats. It rejects empty input, trailing characters, nonfinite values
  and any errno reported by conversion. Leading whitespace follows strtof;
  trailing whitespace fails full-token consumption. Range/subnormal and locale
  behavior follow the linked libc, as in version 1.0.0.
- Numeric functions return false for NULL input/output. Numeric outputs change
  only on success. No process locale changes or allocation functions are called
  by this library; select a suitable libc if its internal strtof allocation
  behavior matters to your application.

Abbreviations, case folding, quoted input strings, semicolon command lists,
binary blocks and full SCPI/IEEE 488.2 compliance are not implemented. Output formatting, RS485 addressing/direction, response timeouts,
ADC calibration and bootloader behavior belong to the application.

## Optional error queue

Include `A2D_ScpiErrorQueue.h` and own one `A2D_ScpiErrorQueue` in your
instrument context. Initialize it with `a2d_scpi_errors_clear`, or zero-initialize
it. Use `a2d_scpi_errors_push` to record a nonzero int16_t error. Zero is ignored.
`peek` returns the oldest code without removing it; `pop` removes it. Both return
zero for an empty queue. `clear` discards all entries. NULL queues are tolerated:
mutators do nothing and readers return zero.

The capacity is eight. Overflow replaces the newest occupied slot with -350,
retaining the oldest seven entries. Further errors while full retain that
marker. Popping frees a slot for a subsequent error. Separate contexts are
independent; no global state, allocation or hardware functions are used. The
API is intended for serialized main-loop use, not concurrent interrupt access.

Your project owns code/message mapping, error-source policy, command dispatch,
and response formatting. The parser does not automatically record errors.
For reliable output, peek when preparing `SYST:ERR?` and pop only after the
complete reply is accepted by your output adapter. The standalone example has
a synchronous reply callback. Do not reset an active instrument's error queue
merely because a communications session closes. The ADC application's queue is
shared by its two interfaces and clears on `*CLS` or MCU restart.

## Verification

The C11 tests include 16 groups covering initialization/NULL arguments,
strict query/comma syntax, numeric conversion, atomic parse failure, FIFO wrap/overflow,
cached/partial/overflow reader reset, and bounded work/CRLF capacity.
They use fixed host buffers and fake callback inputs, without any ADC or HAL
source or include path.

```text
gcc -std=c11 -Wall -Wextra -Werror -pedantic -Isrc -Iexamples/portable_instrument src/A2D_Scpi.c src/A2D_ScpiLineReader.c src/A2D_ScpiErrorQueue.c examples/portable_instrument/Instrument.c tests/test_scpi.c -o test_scpi.exe
./test_scpi.exe
```

Build outputs can be directed to a temporary directory. To verify independence,
copy this complete package to a temporary folder and run the same test and
example builds there using only the copied files. The parent firmware's build
and host-test orchestration are maintained outside this library.
