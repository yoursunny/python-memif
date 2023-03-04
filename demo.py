import importlib.util
import sys
import time

spec = importlib.util.spec_from_file_location(
    "memif", "./build/lib.linux-x86_64-cpython-39/memif.cpython-39-x86_64-linux-gnu.so")
memif = importlib.util.module_from_spec(spec)


m = memif.NativeMemif(str(sys.argv[1]), int(
    sys.argv[2]), bool(int(sys.argv[3])))
for i in range(100):
    m.poll()
    time.sleep(0.1)
    if i % 10 == 0:
        m.send(bytes([i] * i))
del m
