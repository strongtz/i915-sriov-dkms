#include <linux/version.h>

#ifdef CONFIG_DRM_XE_PAGEMAP
#undef CONFIG_DRM_XE_PAGEMAP
#endif

#ifdef CONFIG_DRM_XE_GPUSVM
#undef CONFIG_DRM_XE_GPUSVM
#endif

#define CONFIG_DRM_XE_PAGEMAP 1
#define CONFIG_DRM_XE_GPUSVM 1
