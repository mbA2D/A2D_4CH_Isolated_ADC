#include <A2D_Scpi.h>
#include <A2D_ScpiLineReader.h>
#include <Instrument.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstring>
#include <cmath>

#define CHECK(c) do { if (!(c)) throw std::runtime_error(#c); } while (0)
using namespace a2d_scpi;

void append(void *context, const char *line) { *static_cast<std::string *>(context) += line; }
void execute(Instrument &instrument, const char *text, std::string &output) {
    char line[128];
    CHECK(std::strlen(text) < sizeof(line));
    std::strcpy(line, text);
    instrument.execute(line, append, &output);
}
void second_project() {
    Instrument instrument;
    std::string output;
    execute(instrument, "*IDN?", output);
    execute(instrument, "OUTP:STATE 1", output);
    execute(instrument, "OUTP:STATE?", output);
    execute(instrument, "CONF:LIMIT 2.5e1", output);
    CHECK(instrument.enabled() && instrument.limit() == 25.0f);
    CHECK(output == "Example,Relay,DEMO,1.0\r\n1\r\n");
}
void invalid_setters_do_not_mutate() {
    Instrument instrument;
    std::string output;
    for (const char *line : {"OUTP:STATE 1?", "OUTP:STATE -1", "OUTP:STATE 2", "OUTP:STATE 1 extra",
                             "OUTP:STATE? 1", "CONF:LIMIT nan", "CONF:LIMIT 1e50", "CONF:LIMIT -1",
                             "CONF:LIMIT 1?", "MEAS:VOLT 0?"}) {
        output.clear(); execute(instrument, line, output);
        CHECK(output == "ERROR:COMMAND\r\n" && !instrument.enabled() && instrument.limit() == 1.0f);
    }
}
void independent_projects_and_instances() {
    Instrument first, second;
    std::string output;
    execute(first, "OUTP:STATE 1", output);
    execute(second, "OUTP:STATE?", output);
    CHECK(output == "0\r\n" && first.enabled() && !second.enabled());
    const CommandSpec different[] = {{"OUTP:STATE", 55, 0, false}};
    char line[] = "OUTP:STATE";
    ParsedCommand parsed;
    CHECK(parse(line, different, 1, parsed) == ParseResult::Ready && parsed.spec->id == 55);
}
void exact_headers_and_query_policy() {
    const CommandSpec table[] = {{"READ", 1, 1, true}, {"READ?", 2, 1, false}, {"SET", 3, 1, false}};
    ParsedCommand parsed;
    char trailing[] = " \tREAD 7? \r";
    CHECK(parse(trailing, table, 3, parsed) == ParseResult::Ready && parsed.query && !std::strcmp(parsed.arguments[0], "7"));
    char optional[] = "READ 7";
    CHECK(parse(optional, table, 3, parsed) == ParseResult::Ready && !parsed.query);
    char header[] = "READ? 7";
    CHECK(parse(header, table, 3, parsed) == ParseResult::Ready && parsed.query && parsed.spec->id == 2);
    for (const char *text : {"READ 7??", "READ ?7", "SET 1?", "READ? 7?", "READ 7? more"}) {
        char line[32]; std::strcpy(line, text);
        CHECK(parse(line, table, 3, parsed) == ParseResult::InvalidArguments);
    }
    char lower[] = "read 7";
    CHECK(parse(lower, table, 3, parsed) == ParseResult::UnknownCommand);
    char blank[] = " \t\r";
    CHECK(parse(blank, table, 3, parsed) == ParseResult::Empty);
}
void decimal_boundaries() {
    uint32_t value = 99;
    CHECK(unsignedInteger("4294967295", UINT32_MAX, value) && value == UINT32_MAX);
    CHECK(unsignedInteger("+00031", 31, value) && value == 31);
    CHECK(unsignedInteger("0", 0, value) && value == 0);
    for (const char *text : {"4294967296", "999999999999999999999999999", "-0", "-1", "1.0", "0x10", "+", "", "1x", " 1"}) {
        value = 99;
        CHECK(!unsignedInteger(text, UINT32_MAX, value) && value == 99);
    }
    CHECK(!unsignedInteger(nullptr, 31, value));
    CHECK(!unsignedInteger("32", 31, value) && value == 99);
    CHECK(!unsignedInteger("1", 0, value) && value == 99);
}
void finite_float_boundaries() {
    float value = 42;
    CHECK(realNumber("-1.25e-2", value) && std::fabs(value + 0.0125f) < 0.000001f);
    for (const char *text : {"", "nan", "-inf", "1e50", "1e-99", "1.2x", "2?"}) {
        value = 42;
        CHECK(!realNumber(text, value) && value == 42);
    }
    CHECK(!realNumber(nullptr, value) && value == 42);
}
void arity_and_result_lifetime() {
    const CommandSpec table[] = {{"EIGHT", 7, 8, false}, {"BADTABLE", 8, 9, false}};
    ParsedCommand parsed = {};
    char good[] = "EIGHT 0,1,2,3,4,5,6,7";
    CHECK(parse(good, table, 2, parsed) == ParseResult::Ready);
    CHECK(parsed.spec == &table[0] && !std::strcmp(parsed.arguments[7], "7"));
    CHECK(parsed.arguments[0] >= good && parsed.arguments[7] < good + sizeof(good));
    const ParsedCommand previous = parsed;
    for (const char *text : {"EIGHT 0,1,2,3,4,5,6", "EIGHT 0,1,2,3,4,5,6,7,8", "BADTABLE", "MISSING"}) {
        char line[64]; std::strcpy(line, text);
        CHECK(parse(line, table, 2, parsed) != ParseResult::Ready);
        CHECK(parsed.spec == previous.spec && parsed.arguments[0] == previous.arguments[0]);
    }
    // A second parse cannot overwrite tokens in the first caller-owned buffer.
    CHECK(!std::strcmp(previous.arguments[7], "7"));
}
void project_independent_limits() {
    const CommandSpec table[] = {{"THIS:PROJECT:HAS:A:LONGER:HEADER:THAN:THE:ADC", 42, 2, false}};
    char line[] = "THIS:PROJECT:HAS:A:LONGER:HEADER:THAN:THE:ADC 1,, 2\r";
    ParsedCommand parsed;
    CHECK(parse(line, table, 1, parsed) == ParseResult::Ready && parsed.spec->id == 42);
    CHECK(!std::strcmp(parsed.arguments[0], "1") && !std::strcmp(parsed.arguments[1], "2"));
}
void framing_boundaries_and_recovery() {
    LineBuffer<5> line;
    for (char c : std::string("1234")) CHECK(line.push(c) == LineBuffer<5>::Pending);
    CHECK(line.push('\n') == LineBuffer<5>::Complete && !std::strcmp(line.data(), "1234"));
    line.reset();
    for (char c : std::string("12345")) line.push(c);
    CHECK(line.push('\n') == LineBuffer<5>::TooLong);
    line.push('A'); line.push('\r');
    CHECK(line.push('\n') == LineBuffer<5>::Complete && !std::strcmp(line.data(), "A"));
    line.reset(); line.push('B'); line.push('\0'); line.push('C');
    CHECK(line.push('\n') == LineBuffer<5>::TooLong);
    CHECK(line.push('\n') == LineBuffer<5>::Complete && !*line.data());
}
struct Port {
    std::string input;
    size_t readLimit;
    Port() : readLimit(64) {}
    int available() const { return static_cast<int>(input.size()); }
    size_t readBytes(char *output, size_t wanted) {
        const size_t count = (wanted < readLimit ? wanted : readLimit);
        input.copy(output, count); input.erase(0, count); return count;
    }
};
void fragmented_bulk_and_independent_readers() {
    Port first, second;
    LineReader<32> a, b;
    char line[32];
    first.input = "OUTP:STATE "; CHECK(a.poll(first, line) == 0);
    second.input = "*IDN?\n";
    CHECK(b.poll(second, line) == 1 && !std::strcmp(line, "*IDN?"));
    first.input = "1\nOUTP:STATE?\n"; first.readLimit = 2;
    CHECK(a.poll(first, line) == 1 && !std::strcmp(line, "OUTP:STATE 1"));
    CHECK(a.poll(first, line) == 1 && !std::strcmp(line, "OUTP:STATE?"));
    first.input = "*IDN?\n"; first.readLimit = 0;
    CHECK(a.poll(first, line) == 0);
    first.readLimit = 64;
    CHECK(a.poll(first, line) == 1 && !std::strcmp(line, "*IDN?"));
    first.input = std::string(64, 'X') + "\n*IDN?\n";
    CHECK(a.poll(first, line) == 0); // bounded work while discarding
    CHECK(a.poll(first, line) == -1);
    CHECK(a.has_pending_input());
    CHECK(a.poll(first, line) == 1 && !std::strcmp(line, "*IDN?"));
}

int main() {
    void (*tests[])() = {second_project, invalid_setters_do_not_mutate, independent_projects_and_instances,
        exact_headers_and_query_policy, decimal_boundaries, finite_float_boundaries, arity_and_result_lifetime,
        project_independent_limits, framing_boundaries_and_recovery, fragmented_bulk_and_independent_readers};
    for (auto test : tests) test();
    std::cout << "10 portable SCPI groups passed (no Arduino or ADC files)\n";
}
