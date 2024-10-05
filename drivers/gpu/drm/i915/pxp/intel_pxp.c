// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */
#include <linux/workqueue.h>

#include "gem/i915_gem_context.h"

#include "gt/intel_context.h"
#include "gt/intel_gt.h"

#include "i915_drv.h"

#include "intel_pxp.h"
#include "intel_pxp_gsccs.h"
#include "intel_pxp_irq.h"
#include "intel_pxp_regs.h"
#include "intel_pxp_session.h"
#include "intel_pxp_tee.h"
#include "intel_pxp_types.h"

/**
 * DOC: PXP
 *
 * PXP (Protected Xe Path) is a feature available in Gen12 and newer platforms.
 * It allows execution and flip to display of protected (i.e. encrypted)
 * objects. The SW support is enabled via the CONFIG_DRM_I915_PXP kconfig.
 *
 * Objects can opt-in to PXP encryption at creation time via the
 * I915_GEM_CREATE_EXT_PROTECTED_CONTENT create_ext flag. For objects to be
 * correctly protected they must be used in conjunction with a context created
 * with the I915_CONTEXT_PARAM_PROTECTED_CONTENT flag. See the documentation
 * of those two uapi flags for details and restrictions.
 *
 * Protected objects are tied to a pxp session; currently we only support one
 * session, which i915 manages and whose index is available in the uapi
 * (I915_PROTECTED_CONTENT_DEFAULT_SESSION) for use in instructions targeting
 * protected objects.
 * The session is invalidated by the HW when certain events occur (e.g.
 * suspend/resume). When this happens, all the objects that were used with the
 * session are marked as invalid and all contexts marked as using protected
 * content are banned. Any further attempt at using them in an execbuf call is
 * rejected, while flips are converted to black frames.
 *
 * Some of the PXP setup operations are performed by the Management Engine,
 * which is handled by the mei driver; communication between i915 and mei is
 * performed via the mei_pxp component module.
 */

bool intel_pxp_is_supported(const struct intel_pxp *pxp)
{
	return IS_ENABLED(CONFIG_DRM_I915_PXP) && pxp;
}

bool intel_pxp_is_enabled(const struct intel_pxp *pxp)
{
	return IS_ENABLED(CONFIG_DRM_I915_PXP) && pxp && pxp->ce;
}

bool intel_pxp_is_active(const struct intel_pxp *pxp)
{
	return IS_ENABLED(CONFIG_DRM_I915_PXP) && pxp && pxp->arb_session.is_valid;
}

static void kcr_pxp_set_status(const struct intel_pxp *pxp, bool enable)
{
	u32 val = enable ? _MASKED_BIT_ENABLE(KCR_INIT_ALLOW_DISPLAY_ME_WRITES) :
		  _MASKED_BIT_DISABLE(KCR_INIT_ALLOW_DISPLAY_ME_WRITES);

	intel_uncore_write(pxp->ctrl_gt->uncore, KCR_INIT(pxp->kcr_base), val);
}

static void kcr_pxp_enable(const struct intel_pxp *pxp)
{
	kcr_pxp_set_status(pxp, true);
}

static void kcr_pxp_disable(const struct intel_pxp *pxp)
{
	kcr_pxp_set_status(pxp, false);
}

static int create_vcs_context(struct intel_pxp *pxp)
{
	static struct lock_class_key pxp_lock;
	struct intel_gt *gt = pxp->ctrl_gt;
	struct intel_engine_cs *engine;
	struct intel_context *ce;
	int i;

	/*
	 * Find the first VCS engine present. We're guaranteed there is one
	 * if we're in this function due to the check in has_pxp
	 */
	for (i = 0, engine = NULL; !engine; i++)
		engine = gt->engine_class[VIDEO_DECODE_CLASS][i];

	GEM_BUG_ON(!engine || engine->class != VIDEO_DECODE_CLASS);

	ce = intel_engine_create_pinned_context(engine, engine->gt->vm, SZ_4K,
						I915_GEM_HWS_PXP_ADDR,
						&pxp_lock, "pxp_context");
	if (IS_ERR(ce)) {
		drm_err(&gt->i915->drm, "failed to create VCS ctx for PXP\n");
		return PTR_ERR(ce);
	}

	pxp->ce = ce;

	return 0;
}

static void destroy_vcs_context(struct intel_pxp *pxp)
{
	if (pxp->ce)
		intel_engine_destroy_pinned_context(fetch_and_zero(&pxp->ce));
}

static void pxp_init_full(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp->ctrl_gt;
	int ret;

	/*
	 * we'll use the completion to check if there is a termination pending,
	 * so we start it as completed and we reinit it when a termination
	 * is triggered.
	 */
	init_completion(&pxp->termination);
	complete_all(&pxp->termination);

	if (pxp->ctrl_gt->type == GT_MEDIA)
		pxp->kcr_base = MTL_KCR_BASE;
	else
		pxp->kcr_base = GEN12_KCR_BASE;

	intel_pxp_session_management_init(pxp);

	ret = create_vcs_context(pxp);
	if (ret)
		goto out_session;

	if (HAS_ENGINE(pxp->ctrl_gt, GSC0))
		ret = intel_pxp_gsccs_init(pxp);
	else
		ret = intel_pxp_tee_component_init(pxp);
	if (ret)
		goto out_context;

	drm_info(&gt->i915->drm, "Protected Xe Path (PXP) protected content support initialized\n");

	return;

out_context:
	destroy_vcs_context(pxp);
out_session:
	intel_pxp_session_management_fini(pxp);
}

static struct intel_gt *find_gt_for_required_teelink(struct drm_i915_private *i915)
{
	/*
	 * NOTE: Only certain platforms require PXP-tee-backend dependencies
	 * for HuC authentication. For now, its limited to DG2.
	 */
	if (IS_ENABLED(CONFIG_INTEL_MEI_PXP) && IS_ENABLED(CONFIG_INTEL_MEI_GSC) &&
	    intel_huc_is_loaded_by_gsc(&to_gt(i915)->uc.huc) && intel_uc_uses_huc(&to_gt(i915)->uc))
		return to_gt(i915);

	return NULL;
}

static struct intel_gt *find_gt_for_required_protected_content(struct drm_i915_private *i915)
{
	if (!IS_ENABLED(CONFIG_DRM_I915_PXP) || !INTEL_INFO(i915)->has_pxp)
		return NULL;

	/*
	 * For MTL onwards, PXP-controller-GT needs to have a valid GSC engine
	 * on the media GT. NOTE: if we have a media-tile with a GSC-engine,
	 * the VDBOX is already present so skip that check. We also have to
	 * ensure the GSC and HUC firmware are coming online
	 */
	if (i915->media_gt && HAS_ENGINE(i915->media_gt, GSC0) &&
	    intel_uc_fw_is_loadable(&i915->media_gt->uc.gsc.fw) &&
	    intel_uc_fw_is_loadable(&i915->media_gt->uc.huc.fw))
		return i915->media_gt;

	/*
	 * Else we rely on mei-pxp module but only on legacy platforms
	 * prior to having separate media GTs and has a valid VDBOX.
	 */
	if (IS_ENABLED(CONFIG_INTEL_MEI_PXP) && !i915->media_gt && VDBOX_MASK(to_gt(i915)))
		return to_gt(i915);

	return NULL;
}

int intel_pxp_init(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	bool is_full_feature = false;

	/*
	 * NOTE: Get the ctrl_gt before checking intel_pxp_is_supported since
	 * we still need it if PXP's backend tee transport is needed.
	 */
	gt = find_gt_for_required_protected_content(i915);
	if (gt)
		is_full_feature = true;
	else
		gt = find_gt_for_required_teelink(i915);

	if (!gt)
		return -ENODEV;

	/*
	 * At this point, we will either enable full featured PXP capabilities
	 * including session and object management, or we will init the backend tee
	 * channel for internal users such as HuC loading by GSC
	 */
	i915->pxp = kzalloc(sizeof(*i915->pxp), GFP_KERNEL);
	if (!i915->pxp)
		return -ENOMEM;

	/* init common info used by all feature-mode usages*/
	i915->pxp->ctrl_gt = gt;
	mutex_init(&i915->pxp->tee_mutex);

	/*
	 * If full PXP feature is not available but HuC is loaded by GSC on pre-MTL
	 * such as DG2, we can skip the init of the full PXP session/object management
	 * and just init the tee channel.
	 */
	if (is_full_feature)
		pxp_init_full(i915->pxp);
	else
		intel_pxp_tee_component_init(i915->pxp);

	return 0;
}

void intel_pxp_fini(struct drm_i915_private *i915)
{
	if (!i915->pxp)
		return;

	i915->pxp->arb_session.is_valid = false;

	if (HAS_ENGINE(i915->pxp->ctrl_gt, GSC0))
		intel_pxp_gsccs_fini(i915->pxp);
	else
		intel_pxp_tee_component_fini(i915->pxp);

	destroy_vcs_context(i915->pxp);

	intel_pxp_session_management_fini(i915->pxp);

	kfree(i915->pxp);
	i915->pxp = NULL;
}

void intel_pxp_mark_termination_in_progress(struct intel_pxp *pxp)
{
	pxp->hw_state_invalidated = true;
	pxp->arb_session.is_valid = false;
	pxp->arb_session.tag = 0;
	reinit_completion(&pxp->termination);
}

static void pxp_queue_termination(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp->ctrl_gt;

	/*
	 * We want to get the same effect as if we received a termination
	 * interrupt, so just pretend that we did.
	 */
	spin_lock_irq(gt->irq_lock);
	intel_pxp_mark_termination_in_progress(pxp);
	pxp->session_events |= PXP_TERMINATION_REQUEST;
	queue_work(system_unbound_wq, &pxp->session_work);
	spin_unlock_irq(gt->irq_lock);
}

static bool pxp_component_bound(struct intel_pxp *pxp)
{
	bool bound = false;

	mutex_lock(&pxp->tee_mutex);
	if (pxp->pxp_component)
		bound = true;
	mutex_unlock(&pxp->tee_mutex);

	return bound;
}

int intel_pxp_get_backend_timeout_ms(struct intel_pxp *pxp)
{
	if (HAS_ENGINE(pxp->ctrl_gt, GSC0))
		return GSCFW_MAX_ROUND_TRIP_LATENCY_MS;
	else
		return 250;
}

static int __pxp_global_teardown_final(struct intel_pxp *pxp)
{
	int timeout;

	if (!pxp->arb_session.is_valid)
		return 0;
	/*
	 * To ensure synchronous and coherent session teardown completion
	 * in response to suspend or shutdown triggers, don't use a worker.
	 */
	intel_pxp_mark_termination_in_progress(pxp);
	intel_pxp_terminate(pxp, false);

	timeout = intel_pxp_get_backend_timeout_ms(pxp);

	if (!wait_for_completion_timeout(&pxp->termination, msecs_to_jiffies(timeout)))
		return -ETIMEDOUT;

	return 0;
}

static int __pxp_global_teardown_restart(struct intel_pxp *pxp)
{
	int timeout;

	if (pxp->arb_session.is_valid)
		return 0;
	/*
	 * The arb-session is currently inactive and we are doing a reset and restart
	 * due to a runtime event. Use the worker that was designed for this.
	 */
	pxp_queue_termination(pxp);

	timeout = intel_pxp_get_backend_timeout_ms(pxp);

	if (!wait_for_completion_timeout(&pxp->termination, msecs_to_jiffies(timeout)))
		return -ETIMEDOUT;

	return 0;
}

void intel_pxp_end(struct intel_pxp *pxp)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	intel_wakeref_t wakeref;

	if (!intel_pxp_is_enabled(pxp))
		return;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	mutex_lock(&pxp->arb_mutex);

	if (__pxp_global_teardown_final(pxp))
		drm_dbg(&i915->drm, "PXP end timed out\n");

	mutex_unlock(&pxp->arb_mutex);

	intel_pxp_fini_hw(pxp);
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
}

static bool pxp_required_fw_failed(struct intel_pxp *pxp)
{
	if (__intel_uc_fw_status(&pxp->ctrl_gt->uc.huc.fw) == INTEL_UC_FIRMWARE_LOAD_FAIL)
		return true;
	if (HAS_ENGINE(pxp->ctrl_gt, GSC0) &&
	    __intel_uc_fw_status(&pxp->ctrl_gt->uc.gsc.fw) == INTEL_UC_FIRMWARE_LOAD_FAIL)
		return true;

	return false;
}

static bool pxp_fw_dependencies_completed(struct intel_pxp *pxp)
{
	if (HAS_ENGINE(pxp->ctrl_gt, GSC0))
		return intel_pxp_gsccs_is_ready_for_sessions(pxp);

	return pxp_component_bound(pxp);
}

/*
 * this helper is used by both intel_pxp_start and by
 * the GET_PARAM IOCTL that user space calls. Thus, the
 * return values here should match the UAPI spec.
 */
int intel_pxp_get_readiness_status(struct intel_pxp *pxp, int timeout_ms)
{
	if (!intel_pxp_is_enabled(pxp))
		return -ENODEV;

	if (pxp_required_fw_failed(pxp))
		return -ENODEV;

	if (pxp->platform_cfg_is_bad)
		return -ENODEV;

	if (timeout_ms) {
		if (wait_for(pxp_fw_dependencies_completed(pxp), timeout_ms))
			return 2;
	} else if (!pxp_fw_dependencies_completed(pxp)) {
		return 2;
	}
	return 1;
}

/*
 * the arb session is restarted from the irq work when we receive the
 * termination completion interrupt
 */
#define PXP_READINESS_TIMEOUT 250

int intel_pxp_start(struct intel_pxp *pxp)
{
	int ret = 0;

	ret = intel_pxp_get_readiness_status(pxp, PXP_READINESS_TIMEOUT);
	if (ret < 0)
		return ret;
	else if (ret > 1)
		return -EIO; /* per UAPI spec, user may retry later */

	mutex_lock(&pxp->arb_mutex);

	ret = __pxp_global_teardown_restart(pxp);
	if (ret)
		goto unlock;

	/* make sure the compiler doesn't optimize the double access */
	barrier();

	if (!pxp->arb_session.is_valid)
		ret = -EIO;

unlock:
	mutex_unlock(&pxp->arb_mutex);
	return ret;
}

void intel_pxp_init_hw(struct intel_pxp *pxp)
{
	kcr_pxp_enable(pxp);
	intel_pxp_irq_enable(pxp);
}

void intel_pxp_fini_hw(struct intel_pxp *pxp)
{
	kcr_pxp_disable(pxp);
	intel_pxp_irq_disable(pxp);
}

int intel_pxp_key_check(struct intel_pxp *pxp,
			struct drm_i915_gem_object *obj,
			bool assign)
{
	if (!intel_pxp_is_active(pxp))
		return -ENODEV;

	if (!i915_gem_object_is_protected(obj))
		return -EINVAL;

	GEM_BUG_ON(!pxp->key_instance);

	/*
	 * If this is the first time we're using this object, it's not
	 * encrypted yet; it will be encrypted with the current key, so mark it
	 * as such. If the object is already encrypted, check instead if the
	 * used key is still valid.
	 */
	if (!obj->pxp_key_instance && assign)
		obj->pxp_key_instance = pxp->key_instance;

	if (obj->pxp_key_instance != pxp->key_instance)
		return -ENOEXEC;

	return 0;
}

void intel_pxp_invalidate(struct intel_pxp *pxp)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	struct i915_gem_context *ctx, *cn;

	/* ban all contexts marked as protected */
	spin_lock_irq(&i915->gem.contexts.lock);
	list_for_each_entry_safe(ctx, cn, &i915->gem.contexts.list, link) {
		struct i915_gem_engines_iter it;
		struct intel_context *ce;

		if (!kref_get_unless_zero(&ctx->ref))
			continue;

		if (likely(!i915_gem_context_uses_protected_content(ctx))) {
			i915_gem_context_put(ctx);
			continue;
		}

		spin_unlock_irq(&i915->gem.contexts.lock);

		/*
		 * By the time we get here we are either going to suspend with
		 * quiesced execution or the HW keys are already long gone and
		 * in this case it is worthless to attempt to close the context
		 * and wait for its execution. It will hang the GPU if it has
		 * not already. So, as a fast mitigation, we can ban the
		 * context as quick as we can. That might race with the
		 * execbuffer, but currently this is the best that can be done.
		 */
		for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it)
			intel_context_ban(ce, NULL);
		i915_gem_context_unlock_engines(ctx);

		/*
		 * The context has been banned, no need to keep the wakeref.
		 * This is safe from races because the only other place this
		 * is touched is context_release and we're holding a ctx ref
		 */
		if (ctx->pxp_wakeref) {
			intel_runtime_pm_put(&i915->runtime_pm,
					     ctx->pxp_wakeref);
			ctx->pxp_wakeref = 0;
		}

		spin_lock_irq(&i915->gem.contexts.lock);
		list_safe_reset_next(ctx, cn, link);
		i915_gem_context_put(ctx);
	}
	spin_unlock_irq(&i915->gem.contexts.lock);
}

#ifdef CONFIG_DRM_I915_PXP
static int pxp_set_session_status(struct intel_pxp *pxp,
				  struct prelim_drm_i915_pxp_ops *pxp_ops,
				  struct drm_file *drmfile)
{
	struct prelim_drm_i915_pxp_set_session_status_params params;
	struct prelim_drm_i915_pxp_set_session_status_params __user *uparams =
		u64_to_user_ptr(pxp_ops->params);
	u32 session_id;
	int ret = 0;

	if (copy_from_user(&params, uparams, sizeof(params)) != 0)
		return -EFAULT;

	session_id = params.pxp_tag & PRELIM_DRM_I915_PXP_TAG_SESSION_ID_MASK;

	switch (params.req_session_state) {
	case PRELIM_DRM_I915_PXP_REQ_SESSION_ID_INIT:
		ret = intel_pxp_sm_ioctl_reserve_session(pxp, drmfile,
							 params.session_mode,
							 &params.pxp_tag);
		break;
	case PRELIM_DRM_I915_PXP_REQ_SESSION_IN_PLAY:
		ret = intel_pxp_sm_ioctl_mark_session_in_play(pxp, drmfile,
							      session_id);
		break;
	case PRELIM_DRM_I915_PXP_REQ_SESSION_TERMINATE:
		ret = intel_pxp_sm_ioctl_terminate_session(pxp, drmfile,
							   session_id);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret >= 0) {
		pxp_ops->status = ret;

		if (copy_to_user(uparams, &params, sizeof(params)))
			ret = -EFAULT;
		else
			ret = 0;
	}

	return ret;
}

static bool ioctl_buffer_size_valid(u32 size)
{
	return size > 0 && size <= SZ_64K;
}

static int
intel_pxp_ioctl_io_message(struct intel_pxp *pxp, struct drm_file *drmfile,
			   struct prelim_drm_i915_pxp_tee_io_message_params *params)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	void *msg_in = NULL;
	void *msg_out = NULL;
	int ret = 0;

	if (!params->msg_in || !params->msg_out ||
	    !ioctl_buffer_size_valid(params->msg_out_buf_size) ||
	    !ioctl_buffer_size_valid(params->msg_in_size))
		return -EINVAL;

	msg_in = kzalloc(params->msg_in_size, GFP_KERNEL);
	if (!msg_in)
		return -ENOMEM;

	msg_out = kzalloc(params->msg_out_buf_size, GFP_KERNEL);
	if (!msg_out) {
		ret = -ENOMEM;
		goto end;
	}

	if (copy_from_user(msg_in, u64_to_user_ptr(params->msg_in), params->msg_in_size)) {
		drm_dbg(&i915->drm, "Failed to copy_from_user for TEE message\n");
		ret = -EFAULT;
		goto end;
	}

	if (HAS_ENGINE(pxp->ctrl_gt, GSC0))
		ret = intel_pxp_gsccs_client_io_msg(pxp, drmfile,
						    msg_in, params->msg_in_size,
						    msg_out, params->msg_out_buf_size,
						    &params->msg_out_ret_size);
	else
		ret = intel_pxp_tee_io_message(pxp,
					       msg_in, params->msg_in_size,
					       msg_out, params->msg_out_buf_size,
					       &params->msg_out_ret_size);
	if (ret) {
		drm_dbg(&i915->drm, "Failed to send/receive user TEE message\n");
		goto end;
	}

	if (copy_to_user(u64_to_user_ptr(params->msg_out), msg_out, params->msg_out_ret_size)) {
		drm_dbg(&i915->drm, "Failed copy_to_user for TEE message\n");
		ret = -EFAULT;
		goto end;
	}

end:
	kfree(msg_in);
	kfree(msg_out);
	return ret;
}

static int pxp_send_tee_msg(struct intel_pxp *pxp,
			    struct prelim_drm_i915_pxp_ops *pxp_ops,
			    struct drm_file *drmfile)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	struct prelim_drm_i915_pxp_tee_io_message_params params;
	struct prelim_drm_i915_pxp_tee_io_message_params __user *uparams =
		u64_to_user_ptr(pxp_ops->params);
	int ret = 0;

	if (copy_from_user(&params, uparams, sizeof(params)) != 0)
		return -EFAULT;

	ret = intel_pxp_ioctl_io_message(pxp, drmfile, &params);
	if (ret >= 0) {
		pxp_ops->status = ret;

		if (copy_to_user(uparams, &params, sizeof(params)))
			ret = -EFAULT;
		else
			ret = 0;
	} else {
		drm_dbg(&i915->drm, "Failed to send user TEE IO message\n");
	}

	return ret;
}

static int pxp_query_tag(struct intel_pxp *pxp, struct prelim_drm_i915_pxp_ops *pxp_ops)
{
	struct prelim_drm_i915_pxp_query_tag params;
	struct prelim_drm_i915_pxp_query_tag __user *uparams =
		u64_to_user_ptr(pxp_ops->params);
	int ret = 0;

	if (copy_from_user(&params, uparams, sizeof(params)) != 0)
		return -EFAULT;

	ret = intel_pxp_sm_ioctl_query_pxp_tag(pxp, &params.session_is_alive,
					       &params.pxp_tag);
	if (ret >= 0) {
		pxp_ops->status = ret;

		if (copy_to_user(uparams, &params, sizeof(params)))
			ret = -EFAULT;
		else
			ret = 0;
	}

	return ret;
}

static int
pxp_process_host_session_handle_request(struct intel_pxp *pxp,
					struct prelim_drm_i915_pxp_ops *pxp_ops,
					struct drm_file *drmfile)
{
	struct prelim_drm_i915_pxp_host_session_handle_request params;
	struct prelim_drm_i915_pxp_host_session_handle_request __user *uparams =
		u64_to_user_ptr(pxp_ops->params);
	int ret = 0;

	if (copy_from_user(&params, uparams, sizeof(params)) != 0)
		return -EFAULT;

	if (params.request_type != PRELIM_DRM_I915_PXP_GET_HOST_SESSION_HANDLE) {
		ret = PRELIM_DRM_I915_PXP_OP_STATUS_ERROR_INVALID;
		goto error_out;
	}

	/* legacy hw doesn't use this - user space shouldn't be requesting this */
	if (!HAS_ENGINE(pxp->ctrl_gt, GSC0)) {
		ret = PRELIM_DRM_I915_PXP_OP_STATUS_ERROR_INVALID;
		goto error_out;
	}

	intel_pxp_gsccs_get_client_host_session_handle(pxp, drmfile,
						       &params.host_session_handle);
	if (!params.host_session_handle) {
		ret = PRELIM_DRM_I915_PXP_OP_STATUS_ERROR_UNKNOWN;
		drm_warn(&pxp->ctrl_gt->i915->drm, "Host Session Handle allocated 0x0\n");
	}

error_out:
	if (ret >= 0) {
		pxp_ops->status = ret;

		if (copy_to_user(uparams, &params, sizeof(params)))
			ret = -EFAULT;
		else
			ret = 0;
	}

	return ret;
}

static bool pxp_action_needs_arb_session(u32 action)
{
	switch (action) {
	case PRELIM_DRM_I915_PXP_ACTION_HOST_SESSION_HANDLE_REQ:
		return false;
	}

	return true;
}

int i915_pxp_ops_ioctl(struct drm_device *dev, void *data, struct drm_file *drmfile)
{
	int ret = 0;
	struct prelim_drm_i915_pxp_ops *pxp_ops = data;
	struct drm_i915_private *i915 = to_i915(dev);
	struct intel_pxp *pxp = i915->pxp;
	intel_wakeref_t wakeref;

	if (!intel_pxp_is_enabled(pxp))
		return -ENODEV;

	wakeref = intel_runtime_pm_get_if_in_use(&i915->runtime_pm);
	if (!wakeref) {
		drm_dbg(&i915->drm, "pxp ioctl blocked due to state in suspend\n");
		pxp_ops->status = PRELIM_DRM_I915_PXP_OP_STATUS_SESSION_NOT_AVAILABLE;
		return 0;
	}

	if (pxp_action_needs_arb_session(pxp_ops->action)) {
		if (pxp->hw_state_invalidated) {
			drm_dbg(&i915->drm, "pxp ioctl retry required due to state attacked\n");
			pxp_ops->status = PRELIM_DRM_I915_PXP_OP_STATUS_RETRY_REQUIRED;
			goto out_pm;
		}

		if (!intel_pxp_is_active(pxp)) {
			ret = intel_pxp_start(pxp);
			if (ret)
				goto out_pm;
		}
	}

	mutex_lock(&pxp->session_mutex);

	if (HAS_ENGINE(pxp->ctrl_gt, GSC0)) {
		ret = intel_gsccs_alloc_client_resources(pxp, drmfile);
		if (ret) {
			drm_warn(&i915->drm, "GSCCS drm-client allocation failure\n");
			goto out_unlock;
		}
	}

	switch (pxp_ops->action) {
	case PRELIM_DRM_I915_PXP_ACTION_SET_SESSION_STATUS:
		ret = pxp_set_session_status(pxp, pxp_ops, drmfile);
		break;
	case PRELIM_DRM_I915_PXP_ACTION_TEE_IO_MESSAGE:
		ret = pxp_send_tee_msg(pxp, pxp_ops, drmfile);
		break;
	case PRELIM_DRM_I915_PXP_ACTION_QUERY_PXP_TAG:
		ret = pxp_query_tag(pxp, pxp_ops);
		break;
	case PRELIM_DRM_I915_PXP_ACTION_HOST_SESSION_HANDLE_REQ:
		ret = pxp_process_host_session_handle_request(pxp, pxp_ops, drmfile);
		break;
	default:
		ret = -EINVAL;
		break;
	}

out_unlock:
	mutex_unlock(&pxp->session_mutex);
out_pm:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return ret;
}

void intel_pxp_close(struct intel_pxp *pxp, struct drm_file *drmfile)
{
	if (!intel_pxp_is_enabled(pxp) || !drmfile)
		return;

	mutex_lock(&pxp->session_mutex);
	intel_pxp_file_close(pxp, drmfile);
	if (HAS_ENGINE(pxp->ctrl_gt, GSC0))
		intel_gsccs_free_client_resources(pxp, drmfile);
	mutex_unlock(&pxp->session_mutex);
}

#else

void intel_pxp_close(struct intel_pxp *pxp, struct drm_file *drmfile)
{
}

int i915_pxp_ops_ioctl(struct drm_device *dev, void *data, struct drm_file *drmfile)
{
	return -ENODEV;
}
#endif
