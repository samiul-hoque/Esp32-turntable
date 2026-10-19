"""Capture ESP32 serial output; survives USB hiccups by reopening the port.
usage: serial_log.py PORT SECONDS [--reset] [--out FILE]"""
import sys, time, serial

port, seconds = sys.argv[1], float(sys.argv[2])
reset = "--reset" in sys.argv
out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else None
f = open(out, "a", encoding="utf-8", errors="replace") if out else None


def emit(text):
    text = time.strftime("%H:%M:%S ") + text
    print(text, flush=True)
    if f:
        f.write(text + "\n")
        f.flush()


def open_port(do_reset):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False
    s.rts = False
    s.open()
    s.reset_input_buffer()
    if do_reset:  # EN low then high, GPIO0 high (normal boot)
        s.rts = True
        time.sleep(0.1)
        s.rts = False
    return s


end = time.time() + seconds
s = open_port(reset)
buf = b""
while time.time() < end:
    try:
        n = s.in_waiting
        if not n:
            time.sleep(0.02)
            continue
        buf += s.read(n)
    except (serial.SerialException, OSError) as e:
        emit(f"[logger] serial error: {e}; reopening")
        try:
            s.close()
        except Exception:
            pass
        while time.time() < end:
            time.sleep(1)
            try:
                s = open_port(False)
                emit("[logger] reopened")
                break
            except Exception:
                pass
        continue
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        emit(line.decode("utf-8", "replace").rstrip("\r"))
