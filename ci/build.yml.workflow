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

      - name: Build for the Surface RT (Tegra 3)
        run: make

      - name: Build for the Surface 2 / "Surface RT 2" (Tegra 4)
        run: make PLATFORM=tegra114 OUT=dist/tegra114

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

  windows-native:
    # Proves the Windows path of scripts/build.py on every push.
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4

      - uses: actions/setup-python@v5
        with:
          python-version: "3.x"

      - name: Cache the pinned ARM toolchain
        uses: actions/cache@v4
        with:
          path: .toolchain
          key: xpack-arm-none-eabi-gcc-13.2.1-1.1-win32-x64

      - name: Fetch toolchain (no-op when cached)
        run: py scripts/fetch_toolchain.py
        shell: cmd

      - name: Build (Surface RT, Tegra 3)
        run: py scripts/build.py all
        shell: cmd

      - name: Build (Surface 2 / RT 2, Tegra 4)
        run: py scripts/build.py all --platform tegra114 --out dist\tegra114
        shell: cmd

      - name: Show output
        run: dir dist
        shell: cmd
