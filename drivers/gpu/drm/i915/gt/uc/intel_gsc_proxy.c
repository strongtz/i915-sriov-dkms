#include "intel_gsc_proxy.h"

// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/component.h>
#include "drm/i915_gsc_proxy_mei_interface.h"
#include "drm/i915_component.h"

#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"
#include "gt/intel_ring.h"
#include "intel_gsc_fw.h"
#include "intel_gsc_fwif.h"
#include "intel_gsc_uc.h"
#include "i915_drv.h"

/*
 * GSC proxy:
 * The GSC uC needs to communicate with the CSME to perform certain operations.
 * Since the GSC can't perform this communication directly on platforms where it
 * is integrated in GT, i915 needs to transfer the messages from GSC to CSME
 * and back. i915 must manually start the proxy flow after the GSC is loaded to
 * signal to GSC that we're ready to handle its messages and allow it to query
 * its init data from CSME; GSC will then trigger an HECI2 interrupt if it needs
 * to send messages to CSME again.
 * The proxy flow is as follow:
 * 1 - i915 submits a request to GSC asking for the message to CSME
 * 2 - GSC replies with the proxy header + payload for CSME
 * 3 - i915 sends the reply from GSC as-is to CSME via the mei proxy component
 * 4 - CSME replies with the proxy header + payload for GSC
 * 5 - i915 submits a request to GSC with the reply from CSME
 * 6 - GSC replies either with a new header + payload (same as step 2, so we
 *     restart from there) or with an end message.
 */

#define INTEL_GSC_HECI2_H_CSR _MMIO(0x117004)
#define  CSR_H_INTERRUPT_ENABLE	BIT(0)
#define  CSR_H_INTERRUPT_STATUS	BIT(1)
#define  CSR_H_RESET		BIT(4)

/* how long do we wait for the component to load on boot? */
#define GSC_PROXY_INIT_TIMEOUT_MS 20000

/* the protocol supports up to 32K in each direction */
#define GSC_PROXY_BUFFER_SIZE SZ_32K
#define GSC_PROXY_CHANNEL_SIZE (GSC_PROXY_BUFFER_SIZE * 2)
#define GSC_PROXY_MAX_MSG_SIZE (GSC_PROXY_BUFFER_SIZE - sizeof(struct intel_gsc_mtl_header))

struct gsc_proxy_msg {
	struct intel_gsc_mtl_header header;
	struct intel_gsc_proxy_header proxy_header;
} __packed;

static int proxy_send_to_csme(struct intel_gsc_uc *gsc)
{
	struct drm_device *drm = &gsc_uc_to_gt(gsc)->i915->drm;
	struct i915_gsc_proxy_component *comp = gsc->proxy.component;
	struct intel_gsc_mtl_header *hdr;
	void *in = gsc->proxy.to_csme;
	void *out = gsc->proxy.to_gsc;
	u32 in_size;
	int ret;

	/* CSME msg only includes the proxy */
	hdr = in;
	in += sizeof(struct intel_gsc_mtl_header);
	out += sizeof(struct intel_gsc_mtl_header);

	in_size = hdr->message_size - sizeof(struct intel_gsc_mtl_header);

	/* the message must contain at least the proxy header */
	if (in_size < sizeof(struct intel_gsc_proxy_header) ||
	    in_size > GSC_PROXY_MAX_MSG_SIZE) {
		drm_err(drm, "Invalid CSME message size: %u\n", in_size);
		return -EINVAL;
	}

	ret = comp->ops->send(comp->mei_dev, in, in_size);
	if (ret < 0) {
		drm_err(drm, "Failed to send CSME message\n");
		return ret;
	}

	ret = comp->ops->recv(comp->mei_dev, out, GSC_PROXY_MAX_MSG_SIZE);
	if (ret < 0) {
		drm_err(drm, "Failed to receive CSME message\n");
		return ret;
	}

	return ret;
}

static int emit_gsc_proxy_heci_pkt(struct intel_gsc_uc *gsc, struct i915_request *rq, u32 size)
{
	u64 addr_in = i915_ggtt_offset(gsc->proxy.vma);
	u64 addr_out = addr_in + GSC_PROXY_BUFFER_SIZE;
	u32 *cs;

	cs = intel_ring_begin(rq, 8);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = GSC_HECI_CMD_PKT;
	*cs++ = lower_32_bits(addr_in);
	*cs++ = upper_32_bits(addr_in);
	*cs++ = size;
	*cs++ = lower_32_bits(addr_out);
	*cs++ = upper_32_bits(addr_out);
	*cs++ = GSC_PROXY_BUFFER_SIZE;
	*cs++ = 0;

	intel_ring_advance(rq, cs);

	return 0;
}

static int submit_gsc_proxy_request(struct intel_gsc_uc *gsc, u32 size)
{
	struct drm_device *drm = &gsc_uc_to_gt(gsc)->i915->drm;
	struct intel_context *ce = gsc->ce;
	struct i915_request *rq;
	u32 *marker = gsc->proxy.to_csme; /* first dw of the header */
	int err;

	if (!ce)
		return -ENODEV;

	/* the message must contain at least the gsc and proxy headers */
	if (size < sizeof(struct gsc_proxy_msg) || size > GSC_PROXY_BUFFER_SIZE) {
		drm_err(drm, "Invalid GSC proxy message size: %u\n", size);
		return -EINVAL;
	}

	/* clear the message marker */
	*marker = 0;
	wmb();

	/* send the request */
	rq = i915_request_create(ce);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	if (ce->engine->emit_init_breadcrumb) {
		err = ce->engine->emit_init_breadcrumb(rq);
		if (err)
			goto out_rq;
	}

	err = emit_gsc_proxy_heci_pkt(gsc, rq, size);
	if (err)
		goto out_rq;

	err = ce->engine->emit_flush(rq, 0);
	if (err)
		goto out_rq;

out_rq:
	i915_request_get(rq);

	if (unlikely(err))
		i915_request_set_error_once(rq, err);

	i915_request_add(rq);

	if (!err && i915_request_wait(rq, 0, msecs_to_jiffies(500)) < 0)
		err = -ETIME;

	i915_request_put(rq);

	if (!err) {
		/* wait for the reply to show up*/
		err = wait_for(*marker != 0, 300);
		if (err)
			drm_err(drm, "Failed to get a proxy reply from gsc\n");
	}

	return err;
}

static int validate_proxy_header(struct intel_gsc_proxy_header *header,
				 u32 source, u32 dest)
{
	u32 type = FIELD_GET(GSC_PROXY_TYPE, header->hdr);
	u32 length = FIELD_GET(GSC_PROXY_PAYLOAD_LENGTH, header->hdr);
	int ret = 0;

	if (header->destination != dest || header->source != source) {
		ret = -ENOEXEC;
		goto fail;
	}

	switch (type) {
	case GSC_PROXY_MSG_TYPE_PROXY_PAYLOAD:
		if (length > 0)
			break;
		fallthrough;
	case GSC_PROXY_MSG_TYPE_PROXY_INVALID:
		ret = -EIO;
		goto fail;
	default:
		break;
	}

fail:
	return ret;

}

static int proxy_query(struct intel_gsc_uc *gsc)
{
	struct drm_device *drm = &gsc_uc_to_gt(gsc)->i915->drm;

	struct gsc_proxy_msg *to_gsc = gsc->proxy.to_gsc;
	struct gsc_proxy_msg *to_csme = gsc->proxy.to_csme;
	int ret;

	to_gsc->header.validity_marker = GSC_HECI_VALIDITY_MARKER;
	to_gsc->header.gsc_address  = HECI_MEADDRESS_PROXY;
	to_gsc->header.header_version = MTL_GSC_HEADER_VERSION;
	to_gsc->header.host_session_handle = 0;
	to_gsc->header.message_size = sizeof(struct gsc_proxy_msg);

	to_gsc->proxy_header.hdr =
		FIELD_PREP(GSC_PROXY_TYPE, GSC_PROXY_MSG_TYPE_PROXY_QUERY) |
		FIELD_PREP(GSC_PROXY_PAYLOAD_LENGTH, 0);

	to_gsc->proxy_header.source = GSC_PROXY_ADDRESSING_KMD;
	to_gsc->proxy_header.destination = GSC_PROXY_ADDRESSING_GSC;
	to_gsc->proxy_header.status = 0;

	while (1) {
		/* clear the GSC response header space */
		memset(gsc->proxy.to_csme, 0, sizeof(struct gsc_proxy_msg));

		/* send proxy message to GSC */
		ret = submit_gsc_proxy_request(gsc, to_gsc->header.message_size);
		if (ret) {
			drm_err(drm, "failed to send proxy message to GSC! %d\n", ret);
			goto proxy_error;
		}

		/* stop if this was the last message */
		if (FIELD_GET(GSC_PROXY_TYPE, to_csme->proxy_header.hdr) ==
				GSC_PROXY_MSG_TYPE_PROXY_END)
			break;

		/* make sure the GSC-to-CSME proxy header is sane */
		ret = validate_proxy_header(&to_csme->proxy_header,
					    GSC_PROXY_ADDRESSING_GSC,
					    GSC_PROXY_ADDRESSING_CSME);
		if (ret) {
			drm_err(drm, "invalid GSC to CSME proxy header! %d\n", ret);
			goto proxy_error;
		}

		/* send the GSC message to the CSME */
		ret = proxy_send_to_csme(gsc);
		if (ret < 0) {
			drm_err(drm, "failed to send proxy message to CSME! %d\n", ret);
			goto proxy_error;
		}

		/* update the GSC message size with the returned value from CSME */
		to_gsc->header.message_size = ret + sizeof(struct intel_gsc_mtl_header);

		/* make sure the CSME-to-GSC proxy header is sane */
		ret = validate_proxy_header(&to_gsc->proxy_header,
					    GSC_PROXY_ADDRESSING_CSME,
					    GSC_PROXY_ADDRESSING_GSC);
		if (ret) {
			drm_err(drm, "invalid CSME to GSC proxy header! %d\n", ret);
			goto proxy_error;
		}
	}

proxy_error:
	return ret < 0 ? ret : 0;
}

int intel_gsc_proxy_request_handler(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	int err;

	if (!gsc->proxy.component_added)
		return -ENODEV;

	assert_rpm_wakelock_held(gt->uncore->rpm);

	/* when GSC is loaded, we can queue this before the component is bound */
	wait_for(gsc->proxy.component, GSC_PROXY_INIT_TIMEOUT_MS);

	mutex_lock(&gsc->proxy.mutex);
	if (!gsc->proxy.component) {
		drm_err(&gt->i915->drm,
			"GSC proxy worker called without the component being bound!\n");
		err = -EIO;
	} else {
		/*
		 * write the status bit to clear it and allow new proxy
		 * interrupts to be generated while we handle the current
		 * request, but be sure not to write the reset bit
		 */
		intel_uncore_rmw(gt->uncore, INTEL_GSC_HECI2_H_CSR,
				 CSR_H_RESET, CSR_H_INTERRUPT_STATUS);
		err = proxy_query(gsc);
	}
	mutex_unlock(&gsc->proxy.mutex);
	return err;
}

void intel_gsc_proxy_irq_handler(struct intel_gsc_uc *gsc, u32 iir)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);

	if (unlikely(!iir))
		return;

	lockdep_assert_held(gt->irq_lock);

	if (!gsc->proxy.component) {
		drm_err(&gt->i915->drm,
			"GSC proxy irq received without the component being bound!\n");
		return;
	}

	gsc->gsc_work_actions |= GSC_ACTION_SW_PROXY;
	queue_work(gsc->wq, &gsc->work);
}

static int i915_gsc_proxy_component_bind(struct device *i915_kdev,
					 struct device *tee_kdev, void *data)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);
	struct intel_gt *gt = i915->media_gt;
	struct intel_gsc_uc *gsc = &gt->uc.gsc;

	/* enable HECI2 IRQs */
	intel_uncore_rmw(gt->uncore, INTEL_GSC_HECI2_H_CSR,
			 0, CSR_H_INTERRUPT_ENABLE);

	mutex_lock(&gsc->proxy.mutex);
	gsc->proxy.component = data;
	gsc->proxy.component->mei_dev = tee_kdev;
	mutex_unlock(&gsc->proxy.mutex);

	return 0;
}

static void i915_gsc_proxy_component_unbind(struct device *i915_kdev,
					    struct device *tee_kdev, void *data)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);
	struct intel_gt *gt = i915->media_gt;
	struct intel_gsc_uc *gsc = &gt->uc.gsc;

	mutex_lock(&gsc->proxy.mutex);
	gsc->proxy.component = NULL;
	mutex_unlock(&gsc->proxy.mutex);

	/* disable HECI2 IRQs */
	intel_uncore_rmw(gt->uncore, INTEL_GSC_HECI2_H_CSR,
			 CSR_H_INTERRUPT_ENABLE, 0);
}

static const struct component_ops i915_gsc_proxy_component_ops = {
	.bind   = i915_gsc_proxy_component_bind,
	.unbind = i915_gsc_proxy_component_unbind,
};

static int proxy_channel_alloc(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct i915_vma *vma;
	void *vaddr;
	int err;

	err = intel_guc_allocate_and_map_vma(&gt->uc.guc, GSC_PROXY_CHANNEL_SIZE,
					     &vma, &vaddr);
	if (err)
		return err;

	gsc->proxy.vma = vma;
	gsc->proxy.to_gsc = vaddr;
	gsc->proxy.to_csme = vaddr + GSC_PROXY_BUFFER_SIZE;

	return 0;
}

static void proxy_channel_free(struct intel_gsc_uc *gsc)
{
	if (!gsc->proxy.vma)
		return;

	gsc->proxy.to_gsc = NULL;
	gsc->proxy.to_csme = NULL;
	i915_vma_unpin_and_release(&gsc->proxy.vma, I915_VMA_RELEASE_MAP);
}

void intel_gsc_proxy_fini(struct intel_gsc_uc *gsc)
{
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct drm_i915_private *i915 = gt->i915;

	if (fetch_and_zero(&gsc->proxy.component_added))
		component_del(i915->drm.dev, &i915_gsc_proxy_component_ops);

	proxy_channel_free(gsc);
}

int intel_gsc_proxy_init(struct intel_gsc_uc *gsc)
{
	int err;
	struct intel_gt *gt = gsc_uc_to_gt(gsc);
	struct drm_i915_private *i915 = gt->i915;

	mutex_init(&gsc->proxy.mutex);

	if (!IS_ENABLED(CONFIG_INTEL_MEI_GSC_PROXY)) {
		drm_info(&i915->drm,
			 "can't init GSC proxy due to missing mei component\n");
		return -ENODEV;
	}

	err = proxy_channel_alloc(gsc);
	if (err)
		return err;

	err = component_add_typed(i915->drm.dev, &i915_gsc_proxy_component_ops,
				  I915_COMPONENT_GSC_PROXY);
	if (err < 0) {
		drm_err(&i915->drm, "Failed to add GSC_PROXY component (%d)\n", err);
		goto out_free;
	}

	gsc->proxy.component_added = true;

	return 0;

out_free:
	proxy_channel_free(gsc);
	return err;
}

