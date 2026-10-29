"""Print the board's serial output for a few seconds (for Claude Code, which can't use the interactive monitor).
Usage: python tools/readlog.py PORT [seconds]   Opening the port resets the board, so you see it from boot."""
import sys, time
import serial  # pyserial ships with ESP-IDF's Python environment

port = serial.Serial(sys.argv[1], 115200, timeout=0.2)
end = time.time() + float(sys.argv[2]) if len(sys.argv) > 2 else time.time() + 10
while time.time() < end:
    sys.stdout.write(port.read(4096).decode(errors="replace"))
    sys.stdout.flush()
