"""Supported boards - one table shared by the build and the image builder.

"Surface RT" is ambiguous in the wild, so both generations are listed with the
SoC that actually ships in them:

    Surface RT   (2012, first generation)  NVIDIA Tegra 3  (T30)   Cortex-A9
    Surface 2    (2013, "Surface RT 2")    NVIDIA Tegra 4  (T114)  Cortex-A15
"""

PLATFORMS = {
    "tegra30": {
        "mcpu": "cortex-a9",
        "define": "SRTOS_SOC_TEGRA30",
        "soc": "NVIDIA Tegra 3 (T30)",
        "cpu": "4x Cortex-A9 @ 1.3 GHz",
        "board": "Surface RT (2012)",
        "panel": "1366x768",
        "timer": "Cortex-A9 private timer (PPI 29)",
    },
    "tegra114": {
        "mcpu": "cortex-a15",
        "define": "SRTOS_SOC_TEGRA114",
        "soc": "NVIDIA Tegra 4 (T114)",
        "cpu": "4x Cortex-A15 @ 1.8 GHz (+ companion core)",
        "board": "Surface 2 (2013), a.k.a. Surface RT 2",
        "panel": "1920x1080",
        "timer": "ARM generic timer, EL1 physical (PPI 30)",
    },
}

DEFAULT_PLATFORM = "tegra30"


def describe(name):
    p = PLATFORMS[name]
    return "%s - %s, %s" % (p["board"], p["soc"], p["cpu"])
