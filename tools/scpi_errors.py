"""Host checks for queried SCPI errors; never expect a setter acknowledgement."""
import json


def parse_error(response):
    number, separator, quoted = response.partition(",")
    try:
        code = int(number)
        message = json.loads(quoted)
    except (ValueError, json.JSONDecodeError) as error:
        raise RuntimeError(f"Malformed error response: {response!r}") from error
    if not separator or not isinstance(message, str):
        raise RuntimeError(f"Malformed error response: {response!r}")
    return code, message


def expect_error(connection, identity, command, expected, *, send=None, receive=None):
    """Fence a rejected command with two queries; detect any unsolicited output.

    The queue must be empty before this call, except when command is empty and
    the caller has already transmitted the rejected frame.
    """
    send = send or connection.write
    receive = receive or (lambda: connection.readline().decode("ascii").strip())
    payload = command.encode("ascii") if isinstance(command, str) else command
    send(payload + b"\n*IDN?\nSYST:ERR?\n")
    answer = receive()
    if answer != identity:
        raise RuntimeError(f"Unexpected output before identity fence: {answer!r}")
    response = receive()
    code, _ = parse_error(response)
    allowed = (expected,) if isinstance(expected, int) else expected
    if code not in allowed:
        raise RuntimeError(f"{command!r}: queued {response!r}; expected {allowed!r}")
    return code
