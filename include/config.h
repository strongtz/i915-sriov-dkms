#include <linux/version.h>

#include "backport.h"

#define MODULE_ABS_PATH(path) DKMS_MODULE_SOURCE_DIR/path

#ifdef CONFIG_DRM_XE_PAGEMAP
#undef CONFIG_DRM_XE_PAGEMAP
#endif

#ifdef CONFIG_DRM_XE_GPUSVM
#undef CONFIG_DRM_XE_GPUSVM
#endif

#define CONFIG_DRM_XE_PAGEMAP 1
#define CONFIG_DRM_XE_GPUSVM 1
