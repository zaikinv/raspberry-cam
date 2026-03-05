#!/bin/bash
set -euo pipefail

STREAMID="${1:-pitest7x3}"

cd ~/raspberry-cam/live_experiment_3
exec bash ./live.sh "$STREAMID"
