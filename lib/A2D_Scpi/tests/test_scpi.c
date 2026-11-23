#include <A2D_Scpi.h>
#include <A2D_ScpiLineReader.h>
#include <Instrument.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(EXIT_FAILURE); \
} } while (0)

typedef struct { char text[4096]; size_t length; } Output;

static void append(void *context, const char *line) {
    Output *output = context;
    const size_t size = strlen(line);
    CHECK(size < sizeof(output->text) - output->length);
    memcpy(output->text + output->length, line, size + 1);
    output->length += size;
}

static void clear_output(Output *output) {
    output->length = 0;
    output->text[0] = '\0';
}

static void execute(Instrument *instrument, const char *text, Output *output) {
    char line[128];
    CHECK(strlen(text) < sizeof(line));
    strcpy(line, text);
    instrument_execute(instrument, line, append, output);
}

static void second_project(void) {
    Instrument instrument;
    Output output = {{0}, 0};
    instrument_init(&instrument);
    execute(&instrument, "*IDN?", &output);
    execute(&instrument, "OUTP:STATE 1", &output);
    execute(&instrument, "OUTP:STATE?", &output);
    execute(&instrument, "CONF:LIMIT 2.5e1", &output);
    CHECK(instrument.enabled && instrument.limit == 25.0f);
    CHECK(!strcmp(output.text, "Example,Relay,DEMO,1.0\r\n1\r\n"));
}

static void invalid_setters_do_not_mutate(void) {
    static const char *const lines[] = {
        "OUTP:STATE 1?", "OUTP:STATE -1", "OUTP:STATE 2", "OUTP:STATE 1 extra",
        "OUTP:STATE? 1", "CONF:LIMIT nan", "CONF:LIMIT 1e50", "CONF:LIMIT -1",
        "CONF:LIMIT 1?", "MEAS:VOLT 0?"
    };
    Instrument instrument;
    Output output = {{0}, 0};
    instrument_init(&instrument);
    for (size_t i = 0; i < COUNT(lines); ++i) {
        clear_output(&output);
        execute(&instrument, lines[i], &output);
        CHECK(!output.text[0] && a2d_scpi_errors_pop(&instrument.errors) != 0);
        CHECK(!instrument.enabled && instrument.limit == 1.0f);
    }
}

static void independent_projects_and_instances(void) {
    const A2D_ScpiCommandSpec different[] = {{"OUTP:STATE", 55, 0}};
    char line[] = "OUTP:STATE";
    A2D_ScpiParsedCommand parsed;
    Instrument first, second;
    Output output = {{0}, 0};
    instrument_init(&first);
    instrument_init(&second);
    execute(&first, "OUTP:STATE 1", &output);
    execute(&second, "OUTP:STATE?", &output);
    CHECK(!strcmp(output.text, "0\r\n") && first.enabled && !second.enabled);
    CHECK(a2d_scpi_parse(line, different, COUNT(different), &parsed) == A2D_SCPI_READY);
    CHECK(parsed.spec->id == 55);
}

static void exact_headers_and_query_policy(void) {
    const A2D_ScpiCommandSpec table[] = {{"READ?", 2, 1}, {"SET", 3, 1}};
    const char *const invalid[] = {"READ 7", "READ 7?", "READ 7??", "READ ?7", "SET 1?", "READ? 7?", "READ? 7 more", "READ?7"};
    A2D_ScpiParsedCommand parsed;
    char header[] = " \tREAD? 7 \r", setter[] = "SET 1", lower[] = "read? 7", blank[] = " \t\r";
    CHECK(a2d_scpi_parse(header, table, COUNT(table), &parsed) == A2D_SCPI_READY);
    CHECK(parsed.query && parsed.spec->id == 2 && !strcmp(parsed.arguments[0], "7"));
    CHECK(a2d_scpi_parse(setter, table, COUNT(table), &parsed) == A2D_SCPI_READY && !parsed.query);
    for (size_t i = 0; i < COUNT(invalid); ++i) {
        char line[64]; strcpy(line, invalid[i]);
        CHECK(a2d_scpi_parse(line, table, COUNT(table), &parsed) != A2D_SCPI_READY);
    }
    CHECK(a2d_scpi_parse(lower, table, COUNT(table), &parsed) == A2D_SCPI_UNKNOWN_COMMAND);
    CHECK(a2d_scpi_parse(blank, table, COUNT(table), &parsed) == A2D_SCPI_EMPTY);
}

static void decimal_boundaries(void) {
    static const char *const invalid[] = {
        "4294967296", "999999999999999999999999999", "-0", "-1", "1.0",
        "0x10", "+", "", "1x", " 1"
    };
    uint32_t value = 99;
    CHECK(a2d_scpi_unsigned_integer("4294967295", UINT32_MAX, &value) && value == UINT32_MAX);
    CHECK(a2d_scpi_unsigned_integer("+00031", 31, &value) && value == 31);
    CHECK(a2d_scpi_unsigned_integer("0", 0, &value) && value == 0);
    for (size_t i = 0; i < COUNT(invalid); ++i) {
        value = 99;
        CHECK(!a2d_scpi_unsigned_integer(invalid[i], UINT32_MAX, &value) && value == 99);
    }
    CHECK(!a2d_scpi_unsigned_integer(NULL, 31, &value) && value == 99);
    CHECK(!a2d_scpi_unsigned_integer("32", 31, &value) && value == 99);
    CHECK(!a2d_scpi_unsigned_integer("1", 0, &value) && value == 99);
}

static void finite_float_boundaries(void) {
    static const char *const invalid[] = {"", "nan", "-inf", "1e50", "1e-99", "1.2x", "2?"};
    float value = 42;
    CHECK(a2d_scpi_real_number("-1.25e-2", &value) && fabsf(value + 0.0125f) < 0.000001f);
    for (size_t i = 0; i < COUNT(invalid); ++i) {
        value = 42;
        CHECK(!a2d_scpi_real_number(invalid[i], &value) && value == 42);
    }
    CHECK(!a2d_scpi_real_number(NULL, &value) && value == 42);
}

static void check_same_result(const A2D_ScpiParsedCommand *actual,
                              const A2D_ScpiParsedCommand *previous) {
    CHECK(actual->spec == previous->spec && actual->query == previous->query);
    for (size_t i = 0; i < A2D_SCPI_MAX_ARGUMENTS; ++i)
        CHECK(actual->arguments[i] == previous->arguments[i]);
}

static void arity_and_result_lifetime(void) {
    const A2D_ScpiCommandSpec table[] = {{"EIGHT", 7, 8}, {"BADTABLE", 8, 9}};
    static const char *const invalid[] = {
        "EIGHT 0,1,2,3,4,5,6", "EIGHT 0,1,2,3,4,5,6,7,8", "BADTABLE", "MISSING"
    };
    A2D_ScpiParsedCommand parsed = {0};
    A2D_ScpiParsedCommand previous;
    char good[] = "EIGHT 0,1,2,3,4,5,6,7";
    CHECK(a2d_scpi_parse(good, table, COUNT(table), &parsed) == A2D_SCPI_READY);
    CHECK(parsed.spec == &table[0] && !strcmp(parsed.arguments[7], "7"));
    CHECK(parsed.arguments[0] >= good && parsed.arguments[7] < good + sizeof(good));
    previous = parsed;
    for (size_t i = 0; i < COUNT(invalid); ++i) {
        char line[64];
        strcpy(line, invalid[i]);
        CHECK(a2d_scpi_parse(line, table, COUNT(table), &parsed) != A2D_SCPI_READY);
        check_same_result(&parsed, &previous);
    }
    CHECK(!strcmp(previous.arguments[7], "7"));
}

static void project_independent_limits(void) {
    const A2D_ScpiCommandSpec table[] = {
        {"THIS:PROJECT:HAS:A:LONGER:HEADER:THAN:THE:ADC", 42, 2}
    };
    char line[] = "THIS:PROJECT:HAS:A:LONGER:HEADER:THAN:THE:ADC 1, 2\r";
    A2D_ScpiParsedCommand parsed;
    CHECK(a2d_scpi_parse(line, table, COUNT(table), &parsed) == A2D_SCPI_READY && parsed.spec->id == 42);
    CHECK(!strcmp(parsed.arguments[0], "1") && !strcmp(parsed.arguments[1], "2"));
}

static void framing_boundaries_and_recovery(void) {
    A2D_ScpiLineBuffer line;
    char storage[5];
    CHECK(a2d_scpi_line_init(&line, storage, sizeof(storage)));
    for (const char *p = "1234"; *p; ++p) CHECK(a2d_scpi_line_push(&line, *p) == A2D_SCPI_LINE_PENDING);
    CHECK(a2d_scpi_line_push(&line, '\n') == A2D_SCPI_LINE_COMPLETE && !strcmp(line.data, "1234"));
    a2d_scpi_line_reset(&line);
    for (const char *p = "12345"; *p; ++p) a2d_scpi_line_push(&line, *p);
    CHECK(a2d_scpi_line_push(&line, '\n') == A2D_SCPI_LINE_TOO_LONG);
    a2d_scpi_line_push(&line, 'A');
    a2d_scpi_line_push(&line, '\r');
    CHECK(a2d_scpi_line_push(&line, '\n') == A2D_SCPI_LINE_COMPLETE && !strcmp(line.data, "A"));
    a2d_scpi_line_reset(&line);
    a2d_scpi_line_push(&line, 'B');
    a2d_scpi_line_push(&line, '\0');
    a2d_scpi_line_push(&line, 'C');
    CHECK(a2d_scpi_line_push(&line, '\n') == A2D_SCPI_LINE_TOO_LONG);
    CHECK(a2d_scpi_line_push(&line, '\n') == A2D_SCPI_LINE_COMPLETE && !*line.data);
}

typedef struct {
    const char *input;
    size_t size, position, read_limit;
    unsigned reads;
} Port;

static int port_available(void *context) {
    const Port *port = context;
    return (int)(port->size - port->position);
}

static size_t port_read(void *context, char *output, size_t wanted) {
    Port *port = context;
    size_t count = wanted < port->read_limit ? wanted : port->read_limit;
    if (count > port->size - port->position) count = port->size - port->position;
    memcpy(output, port->input + port->position, count);
    port->position += count;
    ++port->reads;
    return count;
}

static void feed(Port *port, const char *input) {
    port->input = input;
    port->size = strlen(input);
    port->position = 0;
}

static void fragmented_bulk_and_independent_readers(void) {
    Port first = {NULL, 0, 0, 64, 0}, second = {NULL, 0, 0, 64, 0};
    const A2D_ScpiInput first_input = {&first, port_available, port_read};
    const A2D_ScpiInput second_input = {&second, port_available, port_read};
    A2D_ScpiLineReader a, b;
    char first_storage[32], second_storage[32], line[32], oversized[72];
    CHECK(a2d_scpi_reader_init(&a, first_storage, sizeof(first_storage)));
    CHECK(a2d_scpi_reader_init(&b, second_storage, sizeof(second_storage)));
    feed(&first, "OUTP:STATE ");
    CHECK(a2d_scpi_reader_poll(&a, &first_input, line) == 0);
    feed(&second, "*IDN?\n");
    CHECK(a2d_scpi_reader_poll(&b, &second_input, line) == 1 && !strcmp(line, "*IDN?"));
    feed(&first, "1\nOUTP:STATE?\n");
    first.read_limit = 2;
    CHECK(a2d_scpi_reader_poll(&a, &first_input, line) == 1 && !strcmp(line, "OUTP:STATE 1"));
    CHECK(a2d_scpi_reader_poll(&a, &first_input, line) == 1 && !strcmp(line, "OUTP:STATE?"));
    feed(&first, "*IDN?\n");
    first.read_limit = 0;
    CHECK(a2d_scpi_reader_poll(&a, &first_input, line) == 0);
    first.read_limit = 64;
    CHECK(a2d_scpi_reader_poll(&a, &first_input, line) == 1 && !strcmp(line, "*IDN?"));
    memset(oversized, 'X', 64);
    strcpy(oversized + 64, "\n*IDN?\n");
    feed(&first, oversized);
    CHECK(a2d_scpi_reader_poll(&a, &first_input, line) == 0);
    CHECK(a2d_scpi_reader_poll(&a, &first_input, line) == -1);
    CHECK(a2d_scpi_reader_has_pending(&a));
    CHECK(a2d_scpi_reader_poll(&a, &first_input, line) == 1 && !strcmp(line, "*IDN?"));
}

static void c_api_initialization_and_null_arguments(void) {
    A2D_ScpiLineBuffer line = {0};
    A2D_ScpiLineReader reader = {0};
    const A2D_ScpiCommandSpec table[] = {{"READ", 1, 0}};
    A2D_ScpiParsedCommand parsed = {0}, previous = {0};
    Port port = {NULL, 0, 0, 64, 0};
    A2D_ScpiInput input = {&port, port_available, port_read};
    char storage[8] = "KEEP", output[8] = "KEEP", text[] = "READ";
    CHECK(!a2d_scpi_line_init(NULL, storage, sizeof(storage)));
    CHECK(!a2d_scpi_line_init(&line, NULL, sizeof(storage)));
    CHECK(!a2d_scpi_line_init(&line, storage, 0));
    CHECK(!a2d_scpi_line_init(&line, storage, 1));
    CHECK(!line.data && !strcmp(storage, "KEEP"));
    CHECK(a2d_scpi_line_push(&line, 'X') == A2D_SCPI_LINE_TOO_LONG);
    CHECK(!a2d_scpi_reader_init(NULL, storage, sizeof(storage)));
    CHECK(!a2d_scpi_reader_init(&reader, storage, 1));
    CHECK(a2d_scpi_reader_poll(&reader, &input, output) == -1);
    CHECK(a2d_scpi_reader_init(&reader, storage, sizeof(storage)));
    CHECK(a2d_scpi_reader_poll(NULL, &input, output) == -1);
    CHECK(a2d_scpi_reader_poll(&reader, NULL, output) == -1);
    CHECK(a2d_scpi_reader_poll(&reader, &input, NULL) == -1);
    input.read = NULL;
    CHECK(a2d_scpi_reader_poll(&reader, &input, output) == -1 && !strcmp(output, "KEEP"));
    a2d_scpi_reader_reset(NULL);
    a2d_scpi_line_reset(NULL);
    CHECK(!a2d_scpi_reader_has_pending(NULL));
    CHECK(a2d_scpi_parse(NULL, table, COUNT(table), &parsed) == A2D_SCPI_UNKNOWN_COMMAND);
    CHECK(a2d_scpi_parse(text, NULL, 1, &parsed) == A2D_SCPI_UNKNOWN_COMMAND);
    check_same_result(&parsed, &previous);
    CHECK(a2d_scpi_parse(text, table, COUNT(table), NULL) == A2D_SCPI_UNKNOWN_COMMAND);
    CHECK(!a2d_scpi_unsigned_integer("1", 1, NULL));
    CHECK(!a2d_scpi_real_number("1", NULL));
}

static void separator_and_numeric_compatibility(void) {
    const A2D_ScpiCommandSpec table[] = {{"SET", 1, 1}, {"READ?", 2, 1}};
    const char *const valid[] = {"SET +0001 \r", "\tREAD? 1 \r"};
    const char *const invalid[] = {"SET,1", "SET 1;SET 0", "READ 1? ,", "READ? 1\n", "SET 1\n", "SET ,1", "SET 1,", "READ? ,1,,"};
    A2D_ScpiParsedCommand parsed;
    float value = 42;
    for (size_t i = 0; i < COUNT(valid); ++i) {
        char text[64]; strcpy(text, valid[i]);
        CHECK(a2d_scpi_parse(text, table, COUNT(table), &parsed) == A2D_SCPI_READY);
        CHECK(parsed.query == (i == 1) && parsed.arguments[1] == NULL);
    }
    for (size_t i = 0; i < COUNT(invalid); ++i) {
        char text[64]; strcpy(text, invalid[i]);
        CHECK(a2d_scpi_parse(text, table, COUNT(table), &parsed) != A2D_SCPI_READY);
    }
    CHECK(a2d_scpi_real_number("0x1.6p1", &value) && value == 2.75f);
    CHECK(a2d_scpi_real_number("\t+2.75e0", &value) && value == 2.75f);
    CHECK(!a2d_scpi_real_number("2.75 ", &value) && value == 2.75f);
    CHECK(a2d_scpi_real_number("-0", &value) && value == 0 && signbit(value));
}

static void strict_comma_fields_and_atomic_rejection(void) {
    const A2D_ScpiCommandSpec table[] = {{"SET", 1, 2}, {"READ?", 2, 2}, {"EVENT", 3, 0}};
    const char *const valid[] = {"SET 1,2", " \tSET\t1 \t, \t2 \r", "READ? +1,-2.5e-3", "EVENT \t"};
    A2D_ScpiParsedCommand parsed = {0};
    for (size_t i = 0; i < COUNT(valid); ++i) {
        char text[64]; strcpy(text, valid[i]);
        CHECK(a2d_scpi_parse(text, table, COUNT(table), &parsed) == A2D_SCPI_READY);
    }
    const A2D_ScpiParsedCommand previous = parsed;
    const char *const invalid[] = {"SET 1 2", "SET 1\t2", "SET ,1,2", "SET 1,,2", "SET 1, ,2", "SET 1,2,", "SET 1,2, \t",
        "SET 1,", "SET ,", "SET 1,2;EVENT", "EVENT;", "EVENT ,", "EVENT  ,  ", "EVENT\nEVENT", "READ? 1?,2", "SET 1,2?"};
    for (size_t i = 0; i < COUNT(invalid); ++i) {
        char text[64]; strcpy(text, invalid[i]);
        CHECK(a2d_scpi_parse(text, table, COUNT(table), &parsed) != A2D_SCPI_READY);
        check_same_result(&parsed, &previous);
    }
}

static void reader_reset_clears_all_state(void) {
    Port port = {NULL, 0, 0, 64, 0};
    const A2D_ScpiInput input = {&port, port_available, port_read};
    A2D_ScpiLineReader reader;
    char storage[8], output[8] = "KEEP";
    CHECK(a2d_scpi_reader_init(&reader, storage, sizeof(storage)));
    feed(&port, "FIRST\nOLD\n");
    CHECK(a2d_scpi_reader_poll(&reader, &input, output) == 1 && !strcmp(output, "FIRST"));
    CHECK(a2d_scpi_reader_has_pending(&reader) && port_available(&port) == 0);
    a2d_scpi_reader_reset(&reader);
    CHECK(!a2d_scpi_reader_has_pending(&reader));
    feed(&port, "NEW\n");
    CHECK(a2d_scpi_reader_poll(&reader, &input, output) == 1 && !strcmp(output, "NEW"));
    feed(&port, "PART");
    CHECK(a2d_scpi_reader_poll(&reader, &input, output) == 0);
    a2d_scpi_reader_reset(&reader);
    feed(&port, "OK\n");
    CHECK(a2d_scpi_reader_poll(&reader, &input, output) == 1 && !strcmp(output, "OK"));
    feed(&port, "XXXXXXXX");
    CHECK(a2d_scpi_reader_poll(&reader, &input, output) == 0 && reader.line.overflow);
    a2d_scpi_reader_reset(&reader);
    feed(&port, "GOOD\n");
    CHECK(a2d_scpi_reader_poll(&reader, &input, output) == 1 && !strcmp(output, "GOOD"));
}

static int continuous_available(void *context) {
    (void)context;
    return A2D_SCPI_LINE_BUDGET;
}

static size_t continuous_read(void *context, char *output, size_t wanted) {
    unsigned *reads = context;
    ++*reads;
    memset(output, 'X', wanted);
    return wanted;
}

static void bounded_work_and_crlf_capacity(void) {
    unsigned reads = 0;
    const A2D_ScpiInput input = {&reads, continuous_available, continuous_read};
    A2D_ScpiLineReader reader;
    A2D_ScpiLineBuffer line;
    char reader_storage[200], output[200] = "KEEP", line_storage[5];
    CHECK(a2d_scpi_reader_init(&reader, reader_storage, sizeof(reader_storage)));
    for (unsigned i = 1; i <= 4; ++i) {
        CHECK(a2d_scpi_reader_poll(&reader, &input, output) == 0 && reads == i);
        CHECK(!strcmp(output, "KEEP"));
    }
    CHECK(reader.line.overflow);
    CHECK(a2d_scpi_line_init(&line, line_storage, sizeof(line_storage)));
    for (const char *p = "123\r"; *p; ++p) a2d_scpi_line_push(&line, *p);
    CHECK(a2d_scpi_line_push(&line, '\n') == A2D_SCPI_LINE_COMPLETE && !strcmp(line.data, "123"));
    a2d_scpi_line_reset(&line);
    for (const char *p = "1234\r"; *p; ++p) a2d_scpi_line_push(&line, *p);
    CHECK(a2d_scpi_line_push(&line, '\n') == A2D_SCPI_LINE_TOO_LONG);
    a2d_scpi_line_push(&line, '\r');
    a2d_scpi_line_push(&line, '\r');
    CHECK(a2d_scpi_line_push(&line, '\n') == A2D_SCPI_LINE_COMPLETE && !strcmp(line.data, "\r"));
}

static void bounded_error_queue(void) {
    A2D_ScpiErrorQueue first = {0}, second = {0};
    a2d_scpi_errors_clear(NULL); a2d_scpi_errors_push(NULL, -102);
    CHECK(!a2d_scpi_errors_peek(NULL) && !a2d_scpi_errors_pop(NULL));
    for (unsigned round = 0; round < 1000; ++round) {
        for (int16_t i = 0; i < 8; ++i) a2d_scpi_errors_push(&first, (int16_t)(100 + i));
        a2d_scpi_errors_push(&first, 0);
        CHECK(first.count == 8 && a2d_scpi_errors_peek(&first) == 100);
        for (unsigned i = 0; i < 20; ++i) a2d_scpi_errors_push(&first, -222);
        for (int16_t i = 0; i < 7; ++i) CHECK(a2d_scpi_errors_pop(&first) == 100 + i);
        CHECK(a2d_scpi_errors_pop(&first) == -350 && !a2d_scpi_errors_pop(&first));
    }
    a2d_scpi_errors_push(&first, -113); a2d_scpi_errors_push(&second, -222);
    a2d_scpi_errors_clear(&first);
    CHECK(!a2d_scpi_errors_pop(&first) && a2d_scpi_errors_pop(&second) == -222);
}

int main(void) {
    const struct { const char *name; void (*run)(void); } tests[] = {
#define TEST(n) {#n, n}
        TEST(bounded_error_queue), TEST(second_project), TEST(invalid_setters_do_not_mutate), TEST(independent_projects_and_instances),
        TEST(exact_headers_and_query_policy), TEST(decimal_boundaries), TEST(finite_float_boundaries),
        TEST(arity_and_result_lifetime), TEST(project_independent_limits), TEST(framing_boundaries_and_recovery),
        TEST(fragmented_bulk_and_independent_readers), TEST(c_api_initialization_and_null_arguments),
        TEST(separator_and_numeric_compatibility),
        TEST(strict_comma_fields_and_atomic_rejection), TEST(reader_reset_clears_all_state), TEST(bounded_work_and_crlf_capacity)
#undef TEST
    };
    for (size_t i = 0; i < COUNT(tests); ++i) {
        tests[i].run();
        printf("PASS %s\n", tests[i].name);
    }
    printf("%u portable SCPI groups passed (no ADC or HAL files)\n",
           (unsigned)COUNT(tests));
    return 0;
}
