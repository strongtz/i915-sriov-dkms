# SPDX-License-Identifier: GPL-2.0
# Makefile for the i915-sriov-dkms compatibility module

subdir-ccflags-y += -include $(src)/export.h

intel_sriov_compat-y += v6.13/drm_print.o \
			v6.13/hrtimer.o

intel_sriov_compat-y += v6.14/drm_dp_helper.o \
			v6.14/drm_print.o \
			v6.14/ttm_bo_vm.o

intel_sriov_compat-y += v6.15/drm_dp_helper.o \
			v6.15/drm_managed.o \
			v6.15/migrate_device.o

intel_sriov_compat-y += v6.16/drm_dp_helper.o

intel_sriov_compat-y += v6.18/drm_gpuvm.o \
			v6.18/drm_ttm_bo.o

intel_sriov_compat-y += v6.19/drm_dp_helper.o \
			v6.19/pci_rebar.o

intel_sriov_compat-y += drm_gpusvm.o \
			drm_pagemap.o

intel_sriov_compat-y += main.o

obj-m := intel_sriov_compat.o
