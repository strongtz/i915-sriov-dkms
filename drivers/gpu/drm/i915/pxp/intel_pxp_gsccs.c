// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2023 Intel Corporation.
 */

#include "gem/i915_gem_internal.h"

#include "gt/intel_context.h"
#include "gt/intel_gt.h"
#include "gt/intel_gtt.h"
#include "gt/uc/intel_gsc_fw.h"
#include "gt/uc/intel_gsc_uc_heci_cmd_submit.h"

#include "i915_drv.h"
#include "intel_pxp.h"
#include "intel_pxp_cmd_interface_42.h"
#include "intel_pxp_cmd_interface_43.h"
#include "intel_pxp_gsccs.h"
#include "intel_pxp_types.h"

struct drm_file;

/**
 * struct gsccs_client_ctx
 *
 * We dont need to allocate multiple execution resources (above struct)
 * for a single drm_client that is executing multiple PXP sessions.
 * So we use a link list of nodes indexed by the drmclient handle
 */
struct gsccs_client_ctx {
	/** @link: achor for linked list. */
	struct list_head link;
	/** @exec: session execution resource for a given client. */
	struct gsccs_session_resources exec;
	/** @drmfile: drm_file handle for a given client. */
	struct drm_file *drmfile;
};

static struct gsccs_client_ctx *
gsccs_find_client_execution_resource(struct intel_pxp *pxp, struct drm_file *drmfile)
{
	struct gsccs_client_ctx *client;

	if (!drmfile)
		return NULL;

	lockdep_assert_held(&pxp->session_mutex);

	list_for_each_entry(client, &pxp->gsccs_clients, link)
		if (client->drmfile == drmfile)
			return client;

	return NULL;
}

static int
gsccs_send_message(struct intel_pxp *pxp,
		   struct gsccs_session_resources *exec_res,
		   void *msg_in, size_t msg_in_size,
		   void *msg_out, size_t msg_out_size_max,
		   size_t *msg_out_len,
		   u64 *gsc_msg_handle_retry);

int
intel_pxp_gsccs_client_io_msg(struct intel_pxp *pxp, struct drm_file *drmfile,
			      void *msg_in, size_t msg_in_size,
			      void *msg_out, size_t msg_out_size_max,
			      u32 *msg_out_len)
{
	struct gsccs_client_ctx *client;
	size_t reply_size;
	int ret;

	if (!drmfile)
		return -EINVAL;

	client = gsccs_find_client_execution_resource(pxp, drmfile);
	if (!client)
		return -EINVAL;

	ret = gsccs_send_message(pxp, &client->exec,
				 msg_in, msg_in_size,
				 msg_out, msg_out_size_max,
				 &reply_size, NULL);
	*msg_out_len = (u32)reply_size;
	return ret;
}

static void
gsccs_destroy_execution_resource(struct intel_pxp *pxp, struct gsccs_session_resources *exec_res);

static
void gsccs_free_client(struct intel_pxp *pxp, struct gsccs_client_ctx *client)
{
	list_del(&client->link);
	gsccs_destroy_execution_resource(pxp, &client->exec);
	kfree(client);
}

void intel_gsccs_free_client_resources(struct intel_pxp *pxp,
				       struct drm_file *drmfile)
{
	struct gsccs_client_ctx *client;

	if (!drmfile)
		return;

	lockdep_assert_held(&pxp->session_mutex);

	client = gsccs_find_client_execution_resource(pxp, drmfile);
	if (client)
		gsccs_free_client(pxp, client);
}

static int
gsccs_allocate_execution_resource(struct intel_pxp *pxp, struct gsccs_session_resources *exec_res,
				  bool is_client_res);

int intel_gsccs_alloc_client_resources(struct intel_pxp *pxp,
				       struct drm_file *drmfile)
{
	struct gsccs_client_ctx *client;
	int ret;

	if (!drmfile)
		return -EINVAL;

	lockdep_assert_held(&pxp->session_mutex);

	client = gsccs_find_client_execution_resource(pxp, drmfile);
	if (client)
		return 0;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	ret = gsccs_allocate_execution_resource(pxp, &client->exec, true);
	if (ret) {
		kfree(client);
		return ret;
	}

	INIT_LIST_HEAD(&client->link);
	client->drmfile = drmfile;
	list_add_tail(&client->link, &pxp->gsccs_clients);

	return 0;
}

static bool
is_fw_err_platform_config(struct intel_pxp *pxp, u32 type)
{
	switch (type) {
	case PXP_STATUS_ERROR_API_VERSION:
	case PXP_STATUS_PLATFCONFIG_KF1_NOVERIF:
	case PXP_STATUS_PLATFCONFIG_KF1_BAD:
		pxp->platform_cfg_is_bad = true;
		return true;
	default:
		break;
	}
	return false;
}

static const char *
fw_err_to_string(u32 type)
{
	switch (type) {
	case PXP_STATUS_ERROR_API_VERSION:
		return "ERR_API_VERSION";
	case PXP_STATUS_NOT_READY:
		return "ERR_NOT_READY";
	case PXP_STATUS_PLATFCONFIG_KF1_NOVERIF:
	case PXP_STATUS_PLATFCONFIG_KF1_BAD:
		return "ERR_PLATFORM_CONFIG";
	default:
		break;
	}
	return NULL;
}

static int
gsccs_send_message(struct intel_pxp *pxp,
		   struct gsccs_session_resources *exec_res,
		   void *msg_in, size_t msg_in_size,
		   void *msg_out, size_t msg_out_size_max,
		   size_t *msg_out_len,
		   u64 *gsc_msg_handle_retry)
{
	struct intel_gt *gt = pxp->ctrl_gt;
	struct drm_i915_private *i915 = gt->i915;
	struct intel_gsc_mtl_header *header;
	struct intel_gsc_heci_non_priv_pkt pkt;
	u32 insert_header_size = 0;
	size_t max_msg_size;
	u32 reply_size;
	int ret;

	if (!exec_res) {
		drm_err(&i915->drm, "gsc send_message with invalid exec_resource\n");
		return -ENODEV;
	} else if (exec_res == &pxp->gsccs_res || (!msg_in && !msg_out)) {
		/*
		 * kernel submissions need population of gsc-mtl-header and
		 * only kernel does host_session_cleanup (on behalf of client
		 * exec_res) identified by the empty packet.
		 */
		insert_header_size = sizeof(*header);
	}

	if (!exec_res->ce)
		return -ENODEV;

	max_msg_size = PXP43_MAX_HECI_INOUT_SIZE - insert_header_size;

	if (msg_in_size > max_msg_size || msg_out_size_max > max_msg_size)
		return -ENOSPC;

	if (!exec_res->pkt_vma || !exec_res->bb_vma)
		return -ENOENT;

	GEM_BUG_ON(exec_res->pkt_vma->size < (2 * PXP43_MAX_HECI_INOUT_SIZE));

	mutex_lock(&pxp->tee_mutex);

	header = exec_res->pkt_vaddr;

	if (insert_header_size) {
		memset(header, 0, sizeof(*header));
		intel_gsc_uc_heci_cmd_emit_mtl_header(header, HECI_MEADDRESS_PXP,
						      msg_in_size + sizeof(*header),
						      exec_res->host_session_handle);
		/* check if this is a host-session-handle cleanup call (empty packet) */
		if (!msg_in && !msg_out)
			header->flags |= GSC_INFLAG_MSG_CLEANUP;
	}


	/* copy caller provided gsc message handle if this is polling for a prior msg completion */
	if (gsc_msg_handle_retry) /* can be null if its a client send-message */
		header->gsc_message_handle = *gsc_msg_handle_retry;

	/* NOTE: zero size packets are used for session-cleanups */
	if (msg_in && msg_in_size)
		memcpy(exec_res->pkt_vaddr + insert_header_size, msg_in, msg_in_size);

	pkt.addr_in = i915_vma_offset(exec_res->pkt_vma);
	pkt.size_in = header->message_size;
	pkt.addr_out = pkt.addr_in + PXP43_MAX_HECI_INOUT_SIZE;
	pkt.size_out = msg_out_size_max + insert_header_size;
	pkt.heci_pkt_vma = exec_res->pkt_vma;
	pkt.bb_vma = exec_res->bb_vma;

	/*
	 * Before submitting, let's clear-out the validity marker on the reply offset.
	 * We use offset PXP43_MAX_HECI_INOUT_SIZE for reply location so point header there.
	 */
	header = exec_res->pkt_vaddr + PXP43_MAX_HECI_INOUT_SIZE;
	if (insert_header_size) /*not for clients */
		header->validity_marker = 0;

	ret = intel_gsc_uc_heci_cmd_submit_nonpriv(&gt->uc.gsc,
						   exec_res->ce, &pkt, exec_res->bb_vaddr,
						   GSC_HECI_REPLY_LATENCY_MS);
	if (ret) {
		drm_err(&i915->drm, "failed to send gsc PXP msg (%d)\n", ret);
		goto unlock;
	}

	/* Response validity marker, status and busyness */
	if (header->validity_marker != GSC_HECI_VALIDITY_MARKER) {
		drm_err(&i915->drm, "gsc PXP reply with invalid validity marker\n");
		ret = -EINVAL;
		goto unlock;
	}

	/* for client messages, we return the output as-is without verifying it */
	if (!insert_header_size)
		goto skip_output_validation;

	if (header->status != 0) {
		drm_dbg(&i915->drm, "gsc PXP reply status has error = 0x%08x\n",
			header->status);
		ret = -EINVAL;
		goto unlock;
	}
	if (gsc_msg_handle_retry && header->flags & GSC_OUTFLAG_MSG_PENDING) {
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

skip_output_validation:

	reply_size = header->message_size - insert_header_size;
	if (reply_size > msg_out_size_max) {
		drm_warn(&i915->drm, "caller with insufficient PXP reply size %u (%zu)\n",
			 reply_size, msg_out_size_max);
		reply_size = msg_out_size_max;
	}

	if (msg_out)
		memcpy(msg_out,
		       exec_res->pkt_vaddr + PXP43_MAX_HECI_INOUT_SIZE + insert_header_size,
		       reply_size);
	if (msg_out_len)
		*msg_out_len = reply_size;

unlock:
	mutex_unlock(&pxp->tee_mutex);
	return ret;
}

static int
gsccs_send_message_retry_complete(struct intel_pxp *pxp,
				  struct gsccs_session_resources *exec_res,
				  void *msg_in, size_t msg_in_size,
				  void *msg_out, size_t msg_out_size_max,
				  size_t *msg_out_len)
{
	u64 gsc_session_retry = 0;
	int ret, tries = 0;

	/*
	 * Keep sending request if GSC firmware was busy. Based on fw specs +
	 * sw overhead (and testing) we expect a worst case pending-bit delay of
	 * GSC_PENDING_RETRY_MAXCOUNT x GSC_PENDING_RETRY_PAUSE_MS millisecs.
	 *
	 * NOTE: this _retry_complete version of send_message is typically
	 * used internally for arb-session management as user-space callers
	 * interacting with GSC-FW is expected to handle pending-bit replies
	 * on their own.
	 */
	do {
		ret = gsccs_send_message(pxp, exec_res,
					 msg_in, msg_in_size, msg_out, msg_out_size_max,
					 msg_out_len, &gsc_session_retry);
		/* Only try again if gsc says so */
		if (ret != -EAGAIN)
			break;

		msleep(GSC_PENDING_RETRY_PAUSE_MS);
	} while (++tries < GSC_PENDING_RETRY_MAXCOUNT);

	return ret;
}

bool intel_pxp_gsccs_is_ready_for_sessions(struct intel_pxp *pxp)
{
	/*
	 * GSC-fw loading, HuC-fw loading, HuC-fw authentication and
	 * GSC-proxy init flow (requiring an mei component driver)
	 * must all occur first before we can start requesting for PXP
	 * sessions. Checking for completion on HuC authentication and
	 * gsc-proxy init flow (the last set of dependencies that
	 * are out of order) will suffice.
	 */
	if (intel_huc_is_fully_authenticated(&pxp->ctrl_gt->uc.huc) &&
	    intel_gsc_uc_fw_proxy_init_done(&pxp->ctrl_gt->uc.gsc, true))
		return true;

	return false;
}

int intel_pxp_gsccs_create_session(struct intel_pxp *pxp,
				   int arb_session_id)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	struct pxp43_create_arb_in msg_in = {0};
	struct pxp43_create_arb_out msg_out = {0};
	int ret;

	msg_in.header.api_version = PXP_APIVER(4, 3);
	msg_in.header.command_id = PXP43_CMDID_INIT_SESSION;
	msg_in.header.stream_id = (FIELD_PREP(PXP43_INIT_SESSION_APPID, arb_session_id) |
				   FIELD_PREP(PXP43_INIT_SESSION_VALID, 1) |
				   FIELD_PREP(PXP43_INIT_SESSION_APPTYPE, 0));
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);
	msg_in.protection_mode = PXP43_INIT_SESSION_PROTECTION_ARB;

	ret = gsccs_send_message_retry_complete(pxp, &pxp->gsccs_res,
						&msg_in, sizeof(msg_in),
						&msg_out, sizeof(msg_out), NULL);
	if (ret) {
		drm_err(&i915->drm, "Failed to init session %d, ret=[%d]\n", arb_session_id, ret);
	} else if (msg_out.header.status != 0) {
		if (is_fw_err_platform_config(pxp, msg_out.header.status)) {
			drm_info_once(&i915->drm,
				      "PXP init-session-%d failed due to BIOS/SOC:0x%08x:%s\n",
				      arb_session_id, msg_out.header.status,
				      fw_err_to_string(msg_out.header.status));
		} else {
			drm_dbg(&i915->drm, "PXP init-session-%d failed 0x%08x:%st:\n",
				arb_session_id, msg_out.header.status,
				fw_err_to_string(msg_out.header.status));
			drm_dbg(&i915->drm, "     cmd-detail: ID=[0x%08x],API-Ver-[0x%08x]\n",
				msg_in.header.command_id, msg_in.header.api_version);
		}
	}

	return ret;
}

static void intel_pxp_gsccs_end_one_fw_session(struct intel_pxp *pxp, u32 session_id)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	struct pxp42_inv_stream_key_in msg_in = {0};
	struct pxp42_inv_stream_key_out msg_out = {0};
	int ret = 0;

	/*
	 * Stream key invalidation reuses the same version 4.2 input/output
	 * command format but firmware requires 4.3 API interaction
	 */
	msg_in.header.api_version = PXP_APIVER(4, 3);
	msg_in.header.command_id = PXP42_CMDID_INVALIDATE_STREAM_KEY;
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);

	msg_in.header.stream_id = FIELD_PREP(PXP_CMDHDR_EXTDATA_SESSION_VALID, 1);
	msg_in.header.stream_id |= FIELD_PREP(PXP_CMDHDR_EXTDATA_APP_TYPE, 0);
	msg_in.header.stream_id |= FIELD_PREP(PXP_CMDHDR_EXTDATA_SESSION_ID, session_id);

	ret = gsccs_send_message_retry_complete(pxp, &pxp->gsccs_res,
						&msg_in, sizeof(msg_in),
						&msg_out, sizeof(msg_out), NULL);
	if (ret) {
		drm_err(&i915->drm, "Failed to inv-stream-key-%u, ret=[%d]\n",
			session_id, ret);
	} else if (msg_out.header.status != 0) {
		if (is_fw_err_platform_config(pxp, msg_out.header.status)) {
			drm_info_once(&i915->drm,
				      "PXP inv-stream-key-%u failed due to BIOS/SOC :0x%08x:%s\n",
				      session_id, msg_out.header.status,
				      fw_err_to_string(msg_out.header.status));
		} else {
			drm_dbg(&i915->drm, "PXP inv-stream-key-%u failed 0x%08x:%s:\n",
				session_id, msg_out.header.status,
				fw_err_to_string(msg_out.header.status));
			drm_dbg(&i915->drm, "     cmd-detail: ID=[0x%08x],API-Ver-[0x%08x]\n",
				msg_in.header.command_id, msg_in.header.api_version);
		}
	}
}

static void
gsccs_cleanup_fw_host_session_handle(struct intel_pxp *pxp,
				     struct gsccs_session_resources *exec_res)
{
	struct drm_i915_private *i915 = pxp->ctrl_gt->i915;
	int ret;

	ret = gsccs_send_message_retry_complete(pxp, exec_res, NULL, 0, NULL, 0, NULL);
	if (ret)
		drm_dbg(&i915->drm, "Failed to send gsccs msg host-session-cleanup: ret=[%d]\n",
			ret);
}

int
intel_pxp_gsccs_get_client_host_session_handle(struct intel_pxp *pxp, struct drm_file *drmfile,
					       u64 *handle)
{
	struct gsccs_client_ctx *n;

	if (!drmfile)
		return -EINVAL;

	n = gsccs_find_client_execution_resource(pxp, drmfile);
	if (!n)
		return -EINVAL;

	*handle = n->exec.host_session_handle;

	return 0;
}

static void
gsccs_destroy_execution_resource(struct intel_pxp *pxp, struct gsccs_session_resources *exec_res)
{
	if (exec_res->host_session_handle)
		gsccs_cleanup_fw_host_session_handle(pxp, exec_res);
	if (exec_res->ce)
		intel_context_put(exec_res->ce);
	if (exec_res->bb_vma)
		i915_vma_unpin_and_release(&exec_res->bb_vma, I915_VMA_RELEASE_MAP);
	if (exec_res->pkt_vma)
		i915_vma_unpin_and_release(&exec_res->pkt_vma, I915_VMA_RELEASE_MAP);
	if (exec_res->vm)
		i915_vm_put(exec_res->vm);

	memset(exec_res, 0, sizeof(*exec_res));
}

void intel_pxp_gsccs_end_fw_sessions(struct intel_pxp *pxp, u32 sessions_mask)
{
	int n;

	for (n = 0; n < INTEL_PXP_MAX_HWDRM_SESSIONS; ++n) {
		if (sessions_mask & BIT(n))
			intel_pxp_gsccs_end_one_fw_session(pxp, n);
	}
}

static int
gsccs_create_buffer(struct intel_gt *gt,
		    struct i915_address_space *vm,
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

	*vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(*vma)) {
		drm_err(&i915->drm, "Failed to vma-instance gsccs backend %s.\n", bufname);
		err = PTR_ERR(*vma);
		goto out_put;
	}

	/* return a virtual pointer */
	*map = i915_gem_object_pin_map_unlocked(obj, intel_gt_coherent_map_type(gt, obj, true));
	if (IS_ERR(*map)) {
		drm_err(&i915->drm, "Failed to map gsccs backend %s.\n", bufname);
		err = PTR_ERR(*map);
		goto out_put;
	}

	/* all PXP sessions commands are treated as non-privileged */
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

static int
gsccs_allocate_execution_resource(struct intel_pxp *pxp, struct gsccs_session_resources *exec_res,
				  bool is_client_res)
{
	struct intel_gt *gt = pxp->ctrl_gt;
	struct intel_engine_cs *engine = gt->engine[GSC0];
	struct i915_ppgtt *ppgtt;
	struct i915_address_space *vm;
	struct intel_context *ce;
	int err = 0;

	/*
	 * First, ensure the GSC engine is present.
	 * NOTE: Backend would only be called with the correct gt.
	 */
	if (!engine)
		return -ENODEV;

	/* if this is for internal arb session, use the main resource */
	if (!is_client_res) {
		exec_res->vm = i915_vm_get(pxp->ctrl_gt->vm);
	} else {
		ppgtt = i915_ppgtt_create(gt, 0);
		if (IS_ERR(ppgtt)) {
			err = IS_ERR(ppgtt);
			return err;
		}
		exec_res->vm = &ppgtt->vm;
	}
	vm = exec_res->vm;

	/*
	 * Now, allocate, pin and map two objects, one for the heci message packet
	 * and another for the batch buffer we submit into GSC engine (that includes the packet).
	 * NOTE: GSC-CS backend is currently only supported on MTL, so we allocate shmem.
	 */
	err = gsccs_create_buffer(pxp->ctrl_gt, vm, "Heci Packet",
				  2 * PXP43_MAX_HECI_INOUT_SIZE,
				  &exec_res->pkt_vma, &exec_res->pkt_vaddr);
	if (err)
		goto free_vm;

	err = gsccs_create_buffer(pxp->ctrl_gt, vm, "Batch Buffer", PAGE_SIZE,
				  &exec_res->bb_vma, &exec_res->bb_vaddr);
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
	ce->vm = i915_vm_get(vm);
	exec_res->ce = ce;

	/* initialize host-session-handle (for all i915-to-gsc-firmware PXP cmds) */
	get_random_bytes(&exec_res->host_session_handle, sizeof(exec_res->host_session_handle));
	/*
	 * To help with debuggability of gsc-firmware log-parsing let's isolate user-space from
	 * kernel space (arb-session-only) commands by prefixing Bit-0 of host-session-handle
	 */
	exec_res->host_session_handle &= ~HOST_SESSION_MASK;
	exec_res->host_session_handle |= HOST_SESSION_PXP_SINGLE;
	if (is_client_res)
		exec_res->host_session_handle |= 1;
	else
		exec_res->host_session_handle &= ~1;

	return 0;

free_batch:
	i915_vma_unpin_and_release(&exec_res->bb_vma, I915_VMA_RELEASE_MAP);
free_pkt:
	i915_vma_unpin_and_release(&exec_res->pkt_vma, I915_VMA_RELEASE_MAP);
free_vm:
	i915_vm_put(exec_res->vm);
	memset(exec_res, 0, sizeof(*exec_res));

	return err;
}

void intel_pxp_gsccs_fini(struct intel_pxp *pxp)
{
	intel_wakeref_t wakeref;
	struct gsccs_client_ctx *client, *client_tmp;

	with_intel_runtime_pm(&pxp->ctrl_gt->i915->runtime_pm, wakeref)
		intel_pxp_fini_hw(pxp);

	list_for_each_entry_safe(client, client_tmp, &pxp->gsccs_clients, link)
		gsccs_free_client(pxp, client);

	gsccs_destroy_execution_resource(pxp, &pxp->gsccs_res);
}

int intel_pxp_gsccs_init(struct intel_pxp *pxp)
{
	int ret;
	intel_wakeref_t wakeref;

	INIT_LIST_HEAD(&pxp->gsccs_clients);
	ret = gsccs_allocate_execution_resource(pxp, &pxp->gsccs_res, false);
	if (!ret) {
		with_intel_runtime_pm(&pxp->ctrl_gt->i915->runtime_pm, wakeref)
			intel_pxp_init_hw(pxp);
	}
	return ret;
}
