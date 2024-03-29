#!/bin/bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"/..

clang-format-15 -i -style=file *.c

autopep8 -i *.py
isort *.py
