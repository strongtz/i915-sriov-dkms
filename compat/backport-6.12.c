/*
 * Copyright (c) 2024
 *
 * Backport functionality introduced in Linux 6.12.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/version.h>
#include <linux/workqueue.h>
#include <drm/drm_print.h>
#include <drm/display/drm_dp_mst_helper.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)

static void
drm_dp_mst_topology_mgr_invalidate_mstb(struct drm_dp_mst_branch *mstb)
{
	struct drm_dp_mst_port *port;

	/* The link address will need to be re-sent on resume */
	mstb->link_address_sent = false;

	list_for_each_entry(port, &mstb->ports, next)
		if (port->mstb)
			drm_dp_mst_topology_mgr_invalidate_mstb(port->mstb);
}

/**
 * drm_dp_mst_topology_queue_probe - Queue a topology probe
 * @mgr: manager to probe
 *
 * Queue a work to probe the MST topology. Driver's should call this only to
 * sync the topology's HW->SW state after the MST link's parameters have
 * changed in a way the state could've become out-of-sync. This is the case
 * for instance when the link rate between the source and first downstream
 * branch device has switched between UHBR and non-UHBR rates. Except of those
 * cases - for instance when a sink gets plugged/unplugged to a port - the SW
 * state will get updated automatically via MST UP message notifications.
 */
void drm_dp_mst_topology_queue_probe(struct drm_dp_mst_topology_mgr *mgr)
{
	mutex_lock(&mgr->lock);
	if (drm_WARN_ON(mgr->dev, !mgr->mst_state || !mgr->mst_primary))
		goto out_unlock;
	drm_dp_mst_topology_mgr_invalidate_mstb(mgr->mst_primary);
  queue_work(system_long_wq, &mgr->work);
out_unlock:
	mutex_unlock(&mgr->lock);
}
#endif