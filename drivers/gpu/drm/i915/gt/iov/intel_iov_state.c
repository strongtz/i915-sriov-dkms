// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "intel_iov.h"
#include "intel_iov_event.h"
#include "intel_iov_state.h"
#include "intel_iov_utils.h"
#include "gt/intel_gt.h"
#include "gt/uc/abi/guc_actions_pf_abi.h"

static void pf_state_worker_func(struct work_struct *w);

/**
 * intel_iov_state_init_early - Allocate structures for VFs state data.
 * @iov: the IOV struct
 *
 * VFs state data is maintained in the flexible array where:
 *   - entry [0] contains state data of the PF (if applicable),
 *   - entries [1..n] contain state data of VF1..VFn::
 *
 *       <--------------------------- 1 + total_vfs ----------->
 *      +-------+-------+-------+-----------------------+-------+
 *      |   0   |   1   |   2   |                       |   n   |
 *      +-------+-------+-------+-----------------------+-------+
 *      |  PF   |  VF1  |  VF2  |      ...     ...      |  VFn  |
 *      +-------+-------+-------+-----------------------+-------+
 *
 * This function can only be called on PF.
 */
void intel_iov_state_init_early(struct intel_iov *iov)
{
	struct intel_iov_data *data;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(iov->pf.state.data);

	INIT_WORK(&iov->pf.state.worker, pf_state_worker_func);

	data = kcalloc(1 + pf_get_totalvfs(iov), sizeof(*data), GFP_KERNEL);
	if (unlikely(!data)) {
		pf_update_status(iov, -ENOMEM, "state");
		return;
	}

	iov->pf.state.data = data;
}

/**
 * intel_iov_state_release - Release structures used VFs data.
 * @iov: the IOV struct
 *
 * Release structures used for VFs data.
 * This function can only be called on PF.
 */
void intel_iov_state_release(struct intel_iov *iov)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));

	cancel_work_sync(&iov->pf.state.worker);
	kfree(fetch_and_zero(&iov->pf.state.data));
}

static void pf_reset_vf_state(struct intel_iov *iov, u32 vfid)
{
	iov->pf.state.data[vfid].state = 0;
}

/**
 * intel_iov_state_reset - Reset VFs data.
 * @iov: the IOV struct
 *
 * Reset VFs data.
 * This function can only be called on PF.
 */
void intel_iov_state_reset(struct intel_iov *iov)
{
	u16 n;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!iov->pf.state.data)
		return;

	for (n = 0; n < 1 + pf_get_totalvfs(iov); n++) {
		pf_reset_vf_state(iov, n);
	}
}

static int guc_action_vf_control_cmd(struct intel_guc *guc, u32 vfid, u32 cmd)
{
	u32 request[PF2GUC_VF_CONTROL_REQUEST_MSG_LEN] = {
		FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
		FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
		FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION, GUC_ACTION_PF2GUC_VF_CONTROL),
		FIELD_PREP(PF2GUC_VF_CONTROL_REQUEST_MSG_1_VFID, vfid),
		FIELD_PREP(PF2GUC_VF_CONTROL_REQUEST_MSG_2_COMMAND, cmd),
	};
	int ret;

	ret = intel_guc_send(guc, request, ARRAY_SIZE(request));
	return ret > 0 ? -EPROTO : ret;
}

static int pf_control_vf(struct intel_iov *iov, u32 vfid, u32 cmd)
{
	struct intel_runtime_pm *rpm = iov_to_gt(iov)->uncore->rpm;
	intel_wakeref_t wakeref;
	int err = -ENONET;

	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(vfid > pf_get_totalvfs(iov));
	GEM_BUG_ON(!vfid);

	with_intel_runtime_pm(rpm, wakeref)
		err = guc_action_vf_control_cmd(iov_to_guc(iov), vfid, cmd);

	return err;
}

static int pf_trigger_vf_flr_start(struct intel_iov *iov, u32 vfid)
{
	int err;

	err = pf_control_vf(iov, vfid, GUC_PF_TRIGGER_VF_FLR_START);
	if (unlikely(err))
		IOV_ERROR(iov, "Failed to start FLR for VF%u (%pe)\n",
			  vfid, ERR_PTR(err));
	return err;
}

static int pf_trigger_vf_flr_finish(struct intel_iov *iov, u32 vfid)
{
	int err;

	err = pf_control_vf(iov, vfid, GUC_PF_TRIGGER_VF_FLR_FINISH);
	if (unlikely(err))
		IOV_ERROR(iov, "Failed to confirm FLR for VF%u (%pe)\n",
			  vfid, ERR_PTR(err));
	return err;
}

static void pf_clear_vf_ggtt_entries(struct intel_iov *iov, u32 vfid)
{
	struct intel_iov_config *config = &iov->pf.provisioning.configs[vfid];
	struct intel_gt *gt = iov_to_gt(iov);

	GEM_BUG_ON(vfid > pf_get_totalvfs(iov));
	lockdep_assert_held(pf_provisioning_mutex(iov));

	if (!drm_mm_node_allocated(&config->ggtt_region))
		return;

	i915_ggtt_set_space_owner(gt->ggtt, vfid, &config->ggtt_region);
}

static int pf_process_vf_flr_finish(struct intel_iov *iov, u32 vfid)
{
	intel_iov_event_reset(iov, vfid);

	mutex_lock(pf_provisioning_mutex(iov));
	pf_clear_vf_ggtt_entries(iov, vfid);
	mutex_unlock(pf_provisioning_mutex(iov));

	return pf_trigger_vf_flr_finish(iov, vfid);
}

/* Return: true if more processing is needed */
static bool pf_process_vf(struct intel_iov *iov, u32 vfid)
{
	unsigned long *state = &iov->pf.state.data[vfid].state;
	int err;

	if (test_and_clear_bit(IOV_VF_NEEDS_FLR_START, state)) {
		err = pf_trigger_vf_flr_start(iov, vfid);
		if (err == -EBUSY) {
			set_bit(IOV_VF_NEEDS_FLR_START, state);
			return true;
		}
		if (err) {
			set_bit(IOV_VF_FLR_FAILED, state);
			clear_bit(IOV_VF_FLR_IN_PROGRESS, state);
			return false;
		}
		return true;
	}

	if (test_and_clear_bit(IOV_VF_FLR_DONE_RECEIVED, state)) {
		set_bit(IOV_VF_NEEDS_FLR_FINISH, state);
		return true;
	}

	if (test_and_clear_bit(IOV_VF_NEEDS_FLR_FINISH, state)) {
		err = pf_process_vf_flr_finish(iov, vfid);
		if (err == -EBUSY) {
			set_bit(IOV_VF_NEEDS_FLR_FINISH, state);
			return true;
		}
		if (err) {
			set_bit(IOV_VF_FLR_FAILED, state);
			clear_bit(IOV_VF_FLR_IN_PROGRESS, state);
			return false;
		}
		clear_bit(IOV_VF_FLR_IN_PROGRESS, state);
		return false;
	}

	return false;
}

static void pf_queue_worker(struct intel_iov *iov)
{
	queue_work(system_unbound_wq, &iov->pf.state.worker);
}

static void pf_process_all_vfs(struct intel_iov *iov)
{
	unsigned int num_vfs = pf_get_totalvfs(iov);
	unsigned int n;
	bool more = false;

	/* only VFs need processing */
	for (n = 1; n <= num_vfs; n++)
		more |= pf_process_vf(iov, n);

	if (more)
		pf_queue_worker(iov);
}

static void pf_state_worker_func(struct work_struct *w)
{
	struct intel_iov *iov = container_of(w, struct intel_iov, pf.state.worker);

	pf_process_all_vfs(iov);
}

/**
 * DOC: VF FLR Flow
 *
 *          PF                        GUC             PCI
 * ========================================================
 *          |                          |               |
 * (1)      |                          |<------- FLR --|
 *          |                          |               :
 * (2)      |<----------- NOTIFY FLR --|
 *         [ ]                         |
 * (3)     [ ]                         |
 *         [ ]                         |
 *          |-- START FLR ------------>|
 *          |                         [ ]
 * (4)      |                         [ ]
 *          |                         [ ]
 *          |<------------- FLR DONE --|
 *         [ ]                         |
 * (5)     [ ]                         |
 *         [ ]                         |
 *          |-- FINISH FLR ----------->|
 *          |                          |
 *
 * Step 1: PCI HW generates interrupt to GuC about VF FLR
 * Step 2: GuC FW sends G2H notification to PF about VF FLR
 * Step 3: PF sends H2G request to GuC to start VF FLR sequence
 * Step 4: GuC FW performs VF FLR cleanups and notifies PF when done
 * Step 5: PF performs VF FLR cleanups and notifies GuC FW when finished
 */

static void pf_init_vf_flr(struct intel_iov *iov, u32 vfid)
{
	unsigned long *state = &iov->pf.state.data[vfid].state;

	if (test_and_set_bit(IOV_VF_FLR_IN_PROGRESS, state)) {
		IOV_DEBUG(iov, "VF%u FLR is already in progress\n", vfid);
		return;
	}

	set_bit(IOV_VF_NEEDS_FLR_START, state);
	pf_queue_worker(iov);
}

static void pf_handle_vf_flr(struct intel_iov *iov, u32 vfid)
{
	struct device *dev = iov_to_dev(iov);
	struct intel_gt *gt;
	unsigned int gtid;

	if (!iov_is_root(iov)) {
		IOV_ERROR(iov, "Unexpected VF%u FLR notification\n", vfid);
		return;
	}

	dev_info(dev, "VF%u FLR\n", vfid);

	for_each_gt(gt, iov_to_i915(iov), gtid)
		pf_init_vf_flr(&gt->iov, vfid);
}

static void pf_handle_vf_flr_done(struct intel_iov *iov, u32 vfid)
{
	unsigned long *state = &iov->pf.state.data[vfid].state;

	set_bit(IOV_VF_FLR_DONE_RECEIVED, state);
	pf_queue_worker(iov);
}

static void pf_handle_vf_pause_done(struct intel_iov *iov, u32 vfid)
{
	struct device *dev = iov_to_dev(iov);

	dev_info(dev, "VF%u %s\n", vfid, "paused");
}

static int pf_handle_vf_event(struct intel_iov *iov, u32 vfid, u32 eventid)
{
	switch (eventid) {
	case GUC_PF_NOTIFY_VF_FLR:
		pf_handle_vf_flr(iov, vfid);
		break;
	case GUC_PF_NOTIFY_VF_FLR_DONE:
		pf_handle_vf_flr_done(iov, vfid);
		break;
	case GUC_PF_NOTIFY_VF_PAUSE_DONE:
		pf_handle_vf_pause_done(iov, vfid);
		break;
	default:
		return -ENOPKG;
	}

	return 0;
}

static int pf_handle_pf_event(struct intel_iov *iov, u32 eventid)
{
	switch (eventid) {
	case GUC_PF_NOTIFY_VF_ENABLE:
		IOV_DEBUG(iov, "VFs %s/%s\n", str_enabled_disabled(true), str_enabled_disabled(false));
		break;
	default:
		return -ENOPKG;
	}

	return 0;
}

/**
 * intel_iov_state_process_guc2pf - Handle VF state notification from GuC.
 * @iov: the IOV struct
 * @msg: message from the GuC
 * @len: length of the message
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_state_process_guc2pf(struct intel_iov *iov,
				   const u32 *msg, u32 len)
{
	u32 vfid;
	u32 eventid;

	GEM_BUG_ON(!len);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_ORIGIN, msg[0]) != GUC_HXG_ORIGIN_GUC);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_MSG_0_TYPE, msg[0]) != GUC_HXG_TYPE_EVENT);
	GEM_BUG_ON(FIELD_GET(GUC_HXG_EVENT_MSG_0_ACTION, msg[0]) != GUC_ACTION_GUC2PF_VF_STATE_NOTIFY);

	if (unlikely(!intel_iov_is_pf(iov)))
		return -EPROTO;

	if (unlikely(FIELD_GET(GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_0_MBZ, msg[0])))
		return -EPFNOSUPPORT;

	if (unlikely(len != GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_LEN))
		return -EPROTO;

	vfid = FIELD_GET(GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_1_VFID, msg[1]);
	eventid = FIELD_GET(GUC2PF_VF_STATE_NOTIFY_EVENT_MSG_2_EVENT, msg[2]);

	if (unlikely(vfid > pf_get_totalvfs(iov)))
		return -EINVAL;

	return vfid ? pf_handle_vf_event(iov, vfid, eventid) : pf_handle_pf_event(iov, eventid);
}

/**
 * intel_iov_state_start_flr - Start VF FLR sequence.
 * @iov: the IOV struct
 * @vfid: VF identifier
 *
 * This function is for PF only.
 */
void intel_iov_state_start_flr(struct intel_iov *iov, u32 vfid)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(vfid > pf_get_totalvfs(iov));
	GEM_BUG_ON(!vfid);

	pf_init_vf_flr(iov, vfid);
}

/**
 * intel_iov_state_no_flr - Test if VF FLR is not in progress.
 * @iov: the IOV struct
 * @vfid: VF identifier
 *
 * This function is for PF only.
 *
 * Return: true if FLR is not pending or in progress.
 */
bool intel_iov_state_no_flr(struct intel_iov *iov, u32 vfid)
{
	GEM_BUG_ON(!intel_iov_is_pf(iov));
	GEM_BUG_ON(vfid > pf_get_totalvfs(iov));
	GEM_BUG_ON(!vfid);

	return !test_bit(IOV_VF_FLR_IN_PROGRESS, &iov->pf.state.data[vfid].state);
}

/**
 * intel_iov_state_pause_vf - Pause VF.
 * @iov: the IOV struct
 * @vfid: VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_state_pause_vf(struct intel_iov *iov, u32 vfid)
{
	return pf_control_vf(iov, vfid, GUC_PF_TRIGGER_VF_PAUSE);
}

/**
 * intel_iov_state_resume_vf - Resume VF.
 * @iov: the IOV struct
 * @vfid: VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_state_resume_vf(struct intel_iov *iov, u32 vfid)
{
	return pf_control_vf(iov, vfid, GUC_PF_TRIGGER_VF_RESUME);
}

/**
 * intel_iov_state_stop_vf - Stop VF.
 * @iov: the IOV struct
 * @vfid: VF identifier
 *
 * This function is for PF only.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_state_stop_vf(struct intel_iov *iov, u32 vfid)
{
	return pf_control_vf(iov, vfid, GUC_PF_TRIGGER_VF_STOP);
}
