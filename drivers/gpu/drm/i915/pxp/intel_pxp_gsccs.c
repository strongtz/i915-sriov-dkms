// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2023 Intel Corporation.
 */

#include "gem/i915_gem_internal.h"

#include "gt/intel_context.h"
#include "gt/uc/intel_gsc_uc_heci_cmd_submit.h"

#include "i915_drv.h"
#include "intel_pxp_cmd_interface_43.h"
#include "intel_pxp_gsccs.h"
#include "intel_pxp_types.h"

static int gsccs_send_message(struct intel_pxp *pxp,
			      void *msg_in, size_t msg_in_size,
			      void *msg_out, size_t msg_out_size_max,
			      size_t *msg_out_len,
			      u64 *gsc_msg_handle_retry)
{
	struct intel_gt *gt = pxp->ctrl_gt;
	struct drm_i915_private *i915 = gt->i915;
	struct gsccs_session_resources *exec =  &pxp->gsccs_res;
	struct intel_gsc_mtl_header *header = exec->pkt_vaddr;
	struct intel_gsc_heci_non_priv_pkt pkt;
	size_t max_msg_size;
	u32 reply_size;
	int ret;

	if (!exec->ce)
		return -ENODEV;

	max_msg_size = PXP43_MAX_HECI_IN_SIZE - sizeof(*header);

	if (msg_in_size > max_msg_size || msg_out_size_max > max_msg_size)
		return -ENOSPC;

	mutex_lock(&pxp->tee_mutex);

	if (!exec->pkt_vma || !exec->bb_vma)
		return -ENOENT;

	memset(header, 0, sizeof(*header));
	intel_gsc_uc_heci_cmd_emit_mtl_header(header, GSC_HECI_MEADDRESS_PXP,
					      msg_in_size + sizeof(*header),
					      exec->host_session_handle);

	/* copy caller provided gsc message handle if this is polling for a prior msg completion */
	header->gsc_message_handle = *gsc_msg_handle_retry;

	memcpy(exec->pkt_vaddr + sizeof(*header), msg_in, msg_in_size);

	pkt.addr_in = i915_vma_offset(exec->pkt_vma);
	pkt.size_in = header->message_size;
	pkt.addr_out = pkt.addr_in + PXP43_MAX_HECI_IN_SIZE;
	pkt.size_out = msg_out_size_max + sizeof(*header);
	pkt.heci_pkt_vma = exec->pkt_vma;
	pkt.bb_vma = exec->bb_vma;

	ret = intel_gsc_uc_heci_cmd_submit_nonpriv(&gt->uc.gsc,
						   exec->ce, &pkt, exec->bb_vaddr, 500);
	if (ret) {
		drm_err(&i915->drm, "failed to send gsc PXP msg (%d)\n", ret);
		goto unlock;
	}

	/* we keep separate location for reply, so get the response header loc first */
	header = exec->pkt_vaddr + PXP43_MAX_HECI_IN_SIZE;

	/* Response validity marker, status and busyness */
	if (header->validity_marker != GSC_HECI_VALIDITY_MARKER) {
		drm_err(&i915->drm, "gsc PXP reply with invalid validity marker\n");
		ret = -EINVAL;
		goto unlock;
	}
	if (header->status != 0) {
		drm_dbg(&i915->drm, "gsc PXP reply status has error = 0x%08x\n",
			header->status);
		ret = -EINVAL;
		goto unlock;
	}
	if (header->flags & GSC_HECI_FLAG_MSG_PENDING) {
		drm_dbg(&i915->drm, "gsc PXP reply is busy\n");
		/*
		 * When the GSC firmware replies with pending bit, it means that the requested
		 * operation has begun but the completion is pending and the caller needs
		 * to re-request with the gsc_message_handle that was returned by the firmware.
		 * until the pending bit is turned off.
		 */
		*gsc_msg_handle_retry = header->gsc_message_handle;
		ret = -EAGAIN;
		goto unlock;
	}

	reply_size = header->message_size - sizeof(*header);
	if (reply_size > msg_out_size_max) {
		drm_warn(&i915->drm, "caller with insufficient PXP reply size %u (%ld)\n",
			 reply_size, msg_out_size_max);
		reply_size = msg_out_size_max;
	} else if (reply_size != msg_out_size_max) {
		drm_dbg(&i915->drm, "caller unexpected PXP reply size %u (%ld)\n",
			reply_size, msg_out_size_max);
	}

	memcpy(msg_out, exec->pkt_vaddr + PXP43_MAX_HECI_IN_SIZE + sizeof(*header),
	       reply_size);
	if (msg_out_len)
		*msg_out_len = reply_size;

unlock:
	mutex_unlock(&pxp->tee_mutex);
	return ret;
}

int intel_pxp_gsccs_create_session(struct intel_pxp *pxp,
				   int arb_session_id)
{
	struct gsccs_session_resources *exec =  &pxp->gsccs_res;
	struct pxp43_create_arb_in msg_in = {0};
	struct pxp43_create_arb_out msg_out = {0};
	u64 gsc_session_retry = 0;
	int ret, tries = 0;

	/* get a unique host-session-handle (used later in HW cmds) at time of session creation */
	get_random_bytes(&exec->host_session_handle, sizeof(exec->host_session_handle));

	msg_in.header.api_version = PXP_APIVER(4, 3);
	msg_in.header.command_id = PXP43_CMDID_INIT_SESSION;
	msg_in.header.stream_id = (FIELD_PREP(PXP43_INIT_SESSION_APPID, arb_session_id) |
				   FIELD_PREP(PXP43_INIT_SESSION_VALID, 1) |
				   FIELD_PREP(PXP43_INIT_SESSION_APPTYPE, 0));
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);
	msg_in.protection_mode = PXP43_INIT_SESSION_PROTECTION_ARB;

	/*
	 * Keep sending request if GSC firmware was busy.
	 * Based on specs, we can expects a worst case pending-bit
	 * delay of 2000 milisecs.
	 */
	do {
		ret = gsccs_send_message(pxp,
					 &msg_in, sizeof(msg_in),
					 &msg_out, sizeof(msg_out), NULL,
					 &gsc_session_retry);
		/* Only try again if gsc says so */
		if (ret != -EAGAIN)
			break;

		msleep(20);
	} while (++tries < 100);

	return ret;
}

static int
gsccs_create_buffer(struct intel_gt *gt,
		    const char *bufname, size_t size,
		    struct i915_vma **vma, void **map)
{
	struct drm_i915_private *i915 = gt->i915;
	struct drm_i915_gem_object *obj;
	int err = 0;

	obj = i915_gem_object_create_internal(i915, size);
	if (IS_ERR(obj)) {
		drm_err(&i915->drm, "Failed to allocate gsccs backend %s.\n", bufname);
		err = PTR_ERR(obj);
		goto out_none;
	}

	*vma = i915_vma_instance(obj, gt->vm, NULL);
	if (IS_ERR(*vma)) {
		drm_err(&i915->drm, "Failed to vma-instance gsccs backend %s.\n", bufname);
		err = PTR_ERR(*vma);
		goto out_put;
	}

	/* return a virtual pointer */
	*map = i915_gem_object_pin_map_unlocked(obj, i915_coherent_map_type(i915, obj, true));
	if (IS_ERR(*map)) {
		drm_err(&i915->drm, "Failed to map gsccs backend %s.\n", bufname);
		err = PTR_ERR(*map);
		goto out_put;
	}

	/* all PXP sessions commands are treated as non-priveleged */
	err = i915_vma_pin(*vma, 0, 0, PIN_USER);
	if (err) {
		drm_err(&i915->drm, "Failed to vma-pin gsccs backend %s.\n", bufname);
		goto out_unmap;
	}

	return 0;

out_unmap:
	i915_gem_object_unpin_map(obj);
out_put:
	i915_gem_object_put(obj);
out_none:
	*vma = NULL;
	*map = NULL;

	return err;
}

static void
gsccs_destroy_execution_resource(struct intel_pxp *pxp)
{
	struct gsccs_session_resources *strm_res = &pxp->gsccs_res;

	if (strm_res->ce)
		intel_context_put(strm_res->ce);
	if (strm_res->bb_vma)
		i915_vma_unpin_and_release(&strm_res->bb_vma, I915_VMA_RELEASE_MAP);
	if (strm_res->pkt_vma)
		i915_vma_unpin_and_release(&strm_res->pkt_vma, I915_VMA_RELEASE_MAP);

	memset(strm_res, 0, sizeof(*strm_res));
}

static int
gsccs_allocate_execution_resource(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp->ctrl_gt;
	struct gsccs_session_resources *strm_res = &pxp->gsccs_res;
	struct intel_engine_cs *engine = gt->engine[GSC0];
	struct intel_context *ce;
	int err = 0;

	/*
	 * First, ensure the GSC engine is present.
	 * NOTE: Backend would only be called with the correct gt.
	 */
	if (!engine)
		return -ENODEV;

	mutex_init(&pxp->tee_mutex);

	/*
	 * Now, allocate, pin and map two objects, one for the heci message packet
	 * and another for the batch buffer we submit into GSC engine (that includes the packet).
	 * NOTE: GSC-CS backend is currently only supported on MTL, so we allocate shmem.
	 */
	err = gsccs_create_buffer(pxp->ctrl_gt, "Heci Packet",
				  PXP43_MAX_HECI_IN_SIZE + PXP43_MAX_HECI_OUT_SIZE,
				  &strm_res->pkt_vma, &strm_res->pkt_vaddr);
	if (err)
		return err;

	err = gsccs_create_buffer(pxp->ctrl_gt, "Batch Buffer", PAGE_SIZE,
				  &strm_res->bb_vma, &strm_res->bb_vaddr);
	if (err)
		goto free_pkt;

	/* Finally, create an intel_context to be used during the submission */
	ce = intel_context_create(engine);
	if (IS_ERR(ce)) {
		drm_err(&gt->i915->drm, "Failed creating gsccs backend ctx\n");
		err = PTR_ERR(ce);
		goto free_batch;
	}
	i915_vm_put(ce->vm);
	ce->vm = i915_vm_get(pxp->ctrl_gt->vm);

	strm_res->ce = ce;

	return 0;

free_pkt:
	i915_vma_unpin_and_release(&strm_res->pkt_vma, I915_VMA_RELEASE_MAP);
free_batch:
	i915_vma_unpin_and_release(&strm_res->bb_vma, I915_VMA_RELEASE_MAP);
	memset(strm_res, 0, sizeof(*strm_res));

	return err;
}

void intel_pxp_gsccs_fini(struct intel_pxp *pxp)
{
	gsccs_destroy_execution_resource(pxp);
}

int intel_pxp_gsccs_init(struct intel_pxp *pxp)
{
	return gsccs_allocate_execution_resource(pxp);
}
