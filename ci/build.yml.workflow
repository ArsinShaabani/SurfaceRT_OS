name: build

on:
  push:
    branches: [ main ]
  pull_request:
  workflow_dispatch:

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Install toolchain
        run: |
          sudo apt-get update -qq
          sudo apt-get install -y -qq gcc-arm-none-eabi python3

      - name: Show versions
        run: |
          arm-none-eabi-gcc --version | head -1
          python3 --version

      - name: Build loader, kernel, modules and images
        run: make

      - name: Run host test suites
        run: make test

      - name: Build the display sink simulator
        run: make sim

      - name: End-to-end display stream (sender -> sink engine)
        run: |
          build/sim/srtos-sim --width 160 --height 120 --frames 3 --out /tmp &
          sleep 1
          python3 host/srdisplay/sender.py --host 127.0.0.1 --source test \
                  --width 160 --height 120 --frames 3
          sleep 1
          test -f /tmp/frame-0001.ppm

      - name: Upload images
        uses: actions/upload-artifact@v4
        with:
          name: srtos-images
          path: |
            dist/BOOTARM.EFI
            dist/MANIFEST.txt
            dist/elf
          if-no-files-found: ignore

      - name: Upload full disk images
        uses: actions/upload-artifact@v4
        with:
          name: srtos-disk-images
          path: |
            dist/esp.img
            dist/srtos-disk.img
            dist/srtos-usb.img
          compression-level: 0
