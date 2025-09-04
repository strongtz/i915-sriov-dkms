# SPDX-License-Identifier: GPL-2.0
# Makefile for the i915-sriov-dkms compatibility module

subdir-ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE="\"INTEL_SRIOV_COMPAT\""

intel_sriov_compat-y += main.o

intel_sriov_compat-y += v6.16/drm_dp_helper.o

intel_sriov_compat-y += v6.17/drm_pagemap.o \
			v6.17/drm_gpusvm.o
			
obj-m := intel_sriov_compat.o
