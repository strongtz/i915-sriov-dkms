# SPDX-License-Identifier: GPL-2.0
# Makefile for the i915-sriov-dkms compatibility module

subdir-ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE="\"INTEL_SRIOV_COMPAT\""

intel_sriov_compat-y += main.o

intel_sriov_compat-y += v6.13/hrtimer.o

intel_sriov_compat-y += v6.14/drm_dp_helper.o \
			v6.14/drm_print.o

intel_sriov_compat-y += v6.15/drm_dp_helper.o \
			v6.15/drm_managed.o \
			v6.15/migrate_device.o

intel_sriov_compat-y += v6.16/drm_dp_helper.o

intel_sriov_compat-y += drm_gpusvm.o \
			drm_pagemap.o

obj-m := intel_sriov_compat.o
