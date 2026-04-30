"""Drive the CircuitPython REPL on the connected Feather over USB serial.

Finds the right COM port by probing each one, sends a Ctrl-C / Ctrl-D pair to
get a clean REPL, runs the requested commands, and prints the captured
output. Used to run hardware_bringup.test() and report the result.
"""

import sys
import time

import serial
from serial.tools import list_ports


COMMANDS = [
    "import hardware_bringup as hb",
    "hb.test()",
]
READ_SECONDS = 18.0


def find_circuitpy_port():
    candidates = []
    for p in list_ports.comports():
        desc = (p.description or "").lower()
        manufacturer = (p.manufacturer or "").lower()
        # CircuitPython USB CDC typically reports "CircuitPython" or "Adafruit"
        if "circuitpython" in desc or "adafruit" in manufacturer or "adafruit" in desc:
            candidates.append(p.device)
    if candidates:
        return candidates[0]
    # Fallback: just try every COM port
    return [p.device for p in list_ports.comports()]


def drain(ser, seconds):
    end = time.monotonic() + seconds
    chunks = []
    while time.monotonic() < end:
        n = ser.in_waiting
        if n:
            chunks.append(ser.read(n).decode("utf-8", errors="replace"))
        else:
            time.sleep(0.05)
    return "".join(chunks)


def run_on_port(port):
    print(f"=== trying {port} ===", flush=True)
    try:
        ser = serial.Serial(port, baudrate=115200, timeout=0.2)
    except serial.SerialException as exc:
        print(f"open failed: {exc}", flush=True)
        return False

    with ser:
        # Send Ctrl-C twice to interrupt any running code, then a CR for prompt.
        ser.write(b"\x03\x03\r\n")
        time.sleep(0.4)
        # Soft reboot to clear any stale state, then re-enter REPL.
        ser.write(b"\x04")
        time.sleep(1.5)
        ser.write(b"\x03\x03\r\n")
        time.sleep(0.4)
        boot = drain(ser, 0.5)
        if "CircuitPython" not in boot and ">>>" not in boot:
            # Not a CircuitPython REPL on this port.
            print(f"no REPL signature on {port}; saw: {boot[:120]!r}", flush=True)
            return False

        print(f"--- REPL ready on {port} ---", flush=True)
        for cmd in COMMANDS:
            ser.write((cmd + "\r\n").encode())
            time.sleep(0.2)

        out = drain(ser, READ_SECONDS)
        print(out, flush=True)
        return True


def main():
    ports = find_circuitpy_port()
    if isinstance(ports, str):
        ports = [ports]
    if not ports:
        print("no serial ports found", file=sys.stderr)
        sys.exit(1)

    for port in ports:
        if run_on_port(port):
            return
    print("could not get a CircuitPython REPL on any port", file=sys.stderr)
    sys.exit(2)


if __name__ == "__main__":
    main()
