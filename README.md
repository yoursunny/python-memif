# Shared Memory Packet Interface (memif) for Node.js

This package is a Python C extension of [libmemif](https://s3-docs.fd.io/vpp/23.06/interfacing/libmemif/), which provides high performance packet transmit and receive between Python and VPP/DPDK applications.
It works on Linux only and requires libmemif 4.0 installed at `/usr/local/lib/libmemif.so`.

## Installation

```bash
git clone https://github.com/FDio/vpp.git
cd vpp
git checkout v23.06
mkdir -p lib/libmemif-build && cd lib/libmemif-build
cmake -G Ninja ../../extras/libmemif
ninja
sudo ninja install
sudo ldconfig

# cd to the virtual environment where you want to install this extension
pip install ~/python-memif
```

## API

```py
from typing import Callable

RxCallback = Callable[[bytes, bool], None]


class NativeMemif:
    def __init__(self, socket_name: str, id: int, rx: RxCallback,
                 *, is_server=False, dataroom=2048, ring_size_log2=10):
        """
        Construct a memif socket and interface.
        """
        pass

    @property
    def up(self) -> bool:
        """
        Return whether the interface is UP.
        """
        pass

    @property
    def counters(self) -> tuple[int, int, int, int, int]:
        """
        Return counters.
        """
        pass

    def poll(self) -> None:
        """
        This should be invoked periodically to process I/O events.
        """
        pass

    def send(self, b: bytes) -> None:
        """
        Transmit a packet.
        """
        pass

    def close(self) -> None:
        """
        Close the memif socket and interface.
        """
        pass
```
