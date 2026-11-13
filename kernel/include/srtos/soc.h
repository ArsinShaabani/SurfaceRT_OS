/*
 * Pick the SoC definition for this build.
 *
 * scripts/build.py passes -DSRTOS_SOC_TEGRA30 or -DSRTOS_SOC_TEGRA114 (and the
 * matching -mcpu), so every driver can simply include <srtos/soc.h> and get the
 * family wide register map plus this SoC's differences.
 */
#ifndef SRTOS_SOC_H
#define SRTOS_SOC_H

#if defined(SRTOS_SOC_TEGRA114)
#include "tegra114.h"
#elif defined(SRTOS_SOC_TEGRA30)
#include "tegra30.h"
#else
#error "no SoC selected: build with --platform tegra30 or --platform tegra114"
#endif

#endif /* SRTOS_SOC_H */
