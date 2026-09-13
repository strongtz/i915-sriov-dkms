# SPDX-License-Identifier: GPL-2.0
# Makefile for the i915-sriov-dkms compatibility module

subdir-ccflags-y += -include $(src)/export.h

intel_sriov_compat-y += v6.19/drm_dp_helper.o \
			v6.19/pci_rebar.o

intel_sriov_compat-y += v7.0/dmabuf_dma_fence.o \
			v7.0/drm_dp_helper.o \
			v7.0/drm_sched_main.o 

intel_sriov_compat-y += v7.1/bitmap.o \
			v7.1/drm_suballoc.o 

intel_sriov_compat-y += drm_gpusvm/drm_gpusvm.o \
			drm_gpusvm/drm_pagemap.o \
			drm_gpusvm/drm_pagemap_util.o

intel_sriov_compat-y += main.o

obj-m := intel_sriov_compat.o
