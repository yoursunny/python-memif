# Shared Memory Packet Interface (memif) for Node.js

This package is a Python C extension of [libmemif](https://s3-docs.fd.io/vpp/23.02/interfacing/libmemif/), which provides high performance packet transmit and receive between Python and VPP/DPDK applications.
It works on Linux only and requires libmemif 4.0 installed at `/usr/local/lib/libmemif.so`.

## Installation

```bash
git clone https://github.com/FDio/vpp.git
git -C vpp checkout v23.02
mkdir libmemif-build && cd libmemif-build
cmake -G Ninja ../vpp/extras/libmemif
ninja
sudo ninja install

: cd to the virtual environment where you want to install this extension
pip install ~/python-memif
```

## API

```py
class NativeMemif:
  def __init__(self, socketName: str, id: int, isServer: bool, rx: Callback[[bytes], None]):
    """
    Construct a memif socket.
    dataroom is hardcoded to 2048.
    ringSize is hardcoded to 1<<10.
    """
    pass

  def poll(self) -> None:
    """
    This should be invoked periodically to process I/O events.
    """
    pass

  def send(self, b: bytes) -> bool:
    """
    Transmit a packet.
    Packet length must not exceed dataroom.
    """
    pass

  def close(self) -> None:
    """
    Close the memif socket.
    """
    pass

```
