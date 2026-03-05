#!/bin/bash
set -euo pipefail

pkill -f "publish.py --framebuffer" 2>/dev/null || true
pkill -f "/home/vdo/raspberry-cam/live_experiment_3/uvc_shm_bridge" 2>/dev/null || true

cd ~/raspberry-cam
exec bash ./stop.sh
