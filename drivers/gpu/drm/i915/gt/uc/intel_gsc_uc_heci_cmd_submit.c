// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/types.h>

#include "gt/intel_context.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"
#include "gt/intel_ring.h"

#include "i915_drv.h"
#include "i915_vma.h"

#include "intel_gsc_uc_heci_cmd_submit.h"

void intel_gsc_uc_heci_cmd_emit_mtl_header(struct intel_gsc_mtl_header *header,
					   u8 heci_client_id, u32 message_size,
					   u64 host_session_id)
{
	host_session_id &= ~GSC_HECI_HOST_SESSION_USAGE_MASK;
	if (heci_client_id == GSC_HECI_MEADDRESS_PXP)
		host_session_id |= GSC_HECI_SESSION_PXP_SINGLE;

	header->validity_marker = GSC_HECI_VALIDITY_MARKER;
	header->heci_client_id = heci_client_id;
	header->host_session_handle = host_session_id;
	header->header_version = MTL_GSC_HECI_HEADER_VERSION;
	header->message_size = message_size;
}

static void
emit_gsc_heci_pkt_nonpriv(u32 *cmd, struct intel_gsc_heci_non_priv_pkt *pkt)
{
	*cmd++ = GSC_HECI_CMD_PKT;
	*cmd++ = lower_32_bits(pkt->addr_in);
	*cmd++ = upper_32_bits(pkt->addr_in);
	*cmd++ = pkt->size_in;
	*cmd++ = lower_32_bits(pkt->addr_out);
	*cmd++ = upper_32_bits(pkt->addr_out);
	*cmd++ = pkt->size_out;
	*cmd++ = 0;
	*cmd++ = MI_BATCH_BUFFER_END;
}

int
intel_gsc_uc_heci_cmd_submit_nonpriv(struct intel_gsc_uc *gsc,
				     struct intel_context *ce,
				     struct intel_gsc_heci_non_priv_pkt *pkt,
				     u32 *cmd, int timeout_ms)
{
	struct intel_engine_cs *eng;
	struct i915_request *rq;
	int err;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	emit_gsc_heci_pkt_nonpriv(cmd, pkt);

	i915_vma_lock(pkt->bb_vma);
	err = i915_vma_move_to_active(pkt->bb_vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(pkt->bb_vma);
	if (err)
		return err;

	i915_vma_lock(pkt->heci_pkt_vma);
	err = i915_vma_move_to_active(pkt->heci_pkt_vma, rq, EXEC_OBJECT_WRITE);
	i915_vma_unlock(pkt->heci_pkt_vma);
	if (err)
		return err;

	eng = rq->context->engine;
	if (eng->emit_init_breadcrumb) {
		err = eng->emit_init_breadcrumb(rq);
		if (err)
			goto out_rq;
	}

	err = eng->emit_bb_start(rq, i915_vma_offset(pkt->bb_vma), PAGE_SIZE, 0);
	if (err)
		goto out_rq;

	err = ce->engine->emit_flush(rq, 0);
	if (err)
		drm_err(&gsc_uc_to_gt(gsc)->i915->drm,
			"Failed emit-flush for gsc-heci-non-priv-pkterr=%d\n", err);

out_rq:
	i915_request_get(rq);

	if (unlikely(err))
		i915_request_set_error_once(rq, err);

	i915_request_add(rq);

	if (!err) {
		if (i915_request_wait(rq, I915_WAIT_INTERRUPTIBLE,
				      msecs_to_jiffies(timeout_ms)) < 0)
			err = -ETIME;
	}

	i915_request_put(rq);

	return err;
}
