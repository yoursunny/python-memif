import importlib.util
import sys
import time

spec = importlib.util.spec_from_file_location(
    "memif", "./build/lib.linux-x86_64-cpython-310/memif.cpython-310-x86_64-linux-gnu.so")
memif = importlib.util.module_from_spec(spec)


def rx(b: bytes, hasNext: bool) -> None:
    print(b.hex(), hasNext)


m = memif.NativeMemif(
    socket_name=str(sys.argv[1]),
    id=int(sys.argv[2]),
    is_server=bool(int(sys.argv[3])),
    rx=rx,
    dataroom=200,
)
for i in range(1000):
    m.poll()
    time.sleep(0.1)
    if i % 10 == 0:
        m.send(bytes([i % 256] * i))
m.close()
