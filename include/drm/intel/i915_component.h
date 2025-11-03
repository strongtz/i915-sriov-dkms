#include_next <drm/intel/i915_component.h>

#ifndef __BACKPORT_INTEL_I915_COMPONENT_H__
#define __BACKPORT_INTEL_I915_COMPONENT_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
#define INTEL_COMPONENT_LB (enum i915_component_type)(I915_COMPONENT_GSC_PROXY + 1)
#endif

#endif
