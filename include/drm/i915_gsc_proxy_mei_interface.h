/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2022 Intel Corporation
 */

#ifndef _I915_GSC_PROXY_MEI_INTERFACE_H_
#define _I915_GSC_PROXY_MEI_INTERFACE_H_

#include <linux/mutex.h>
#include <linux/device.h>

/**
 * struct i915_gsc_proxy_component_ops - ops for GSC Proxy services.
 * @owner: Module providing the ops
 * @send: sends data through GSC proxy
 * @recv: receives data through GSC proxy
 */
struct i915_gsc_proxy_component_ops {
	struct module *owner;

	int (*send)(struct device *dev, const void *buf, size_t size);
	int (*recv)(struct device *dev, void *buf, size_t size);
};

/**
 * struct i915_gsc_proxy_component - Used for communication between i915 and
 * MEI drivers for GSC proxy services
 * @mei_dev: device that provide the GSC proxy service.
 * @proxy_ops: Ops implemented by GSC proxy driver, used by i915 driver.
 */
struct i915_gsc_proxy_component {
	struct device *mei_dev;
	const struct i915_gsc_proxy_component_ops *ops;
};

#endif /* _I915_GSC_PROXY_MEI_INTERFACE_H_ */
