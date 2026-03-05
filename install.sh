#!/bin/bash
set -euo pipefail

echo "=== Raspberry Cam Installer ==="

# ─── 1. Raspberry Ninja ─────────────────────────────────────────────
if [ ! -d ~/raspberry_ninja ]; then
  echo "Installing Raspberry Ninja..."
  cd ~
  git clone https://github.com/nicholasgasior/raspberry-ninja.git raspberry_ninja
  cd raspberry_ninja
  bash install.sh --non-interactive
  cd ~/raspberry-cam
else
  echo "Raspberry Ninja already installed, skipping."
fi

# ─── 2. Dependencies ────────────────────────────────────────────────
echo "Installing dependencies..."
sudo apt-get update
sudo apt-get install -y \
  v4l-utils build-essential imagemagick curl \
  gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly

# ─── 3. v4l2loopback v0.12.5 (v0.15.3 has DQBUF regression) ───────
if [ ! -f ~/v4l2loopback/v4l2loopback.ko ]; then
  echo "Building v4l2loopback v0.12.5..."
  sudo apt-get install -y raspberrypi-kernel-headers || true
  cd ~
  [ -d v4l2loopback ] && rm -rf v4l2loopback
  git clone https://github.com/umlaeute/v4l2loopback.git
  cd v4l2loopback
  git checkout v0.12.5
  make
  cd ~/raspberry-cam
else
  echo "v4l2loopback already built, skipping."
fi

# ─── 4. Compile uvc-gadget ──────────────────────────────────────────
echo "Compiling uvc-gadget..."
make -C src clean
make -C src

# ─── 5. Download sample images ──────────────────────────────────────
echo "Downloading sample images..."
mkdir -p img
for i in 1 2 3 4 5; do
  if [ ! -f "img/frame_${i}.jpg" ]; then
    curl -sL "https://picsum.photos/640/480" -o "img/frame_${i}.jpg"
    sleep 1
  fi
done

echo ""
echo "=== Done! ==="
echo ""
echo "To run:  ./setup.sh && ./run.sh"
echo "To stop: ./stop.sh"
