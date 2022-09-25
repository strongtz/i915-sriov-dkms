// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/slab.h>
#include <linux/stackdepot.h>
#include <linux/stacktrace.h>
#include <linux/sort.h>

#include <drm/drm_print.h>

#include "intel_wakeref.h"

#define STACKDEPTH 8

static noinline depot_stack_handle_t __save_depot_stack(void)
{
	unsigned long entries[STACKDEPTH];
	unsigned int n;

	n = stack_trace_save(entries, ARRAY_SIZE(entries), 1);
	return stack_depot_save(entries, n, GFP_NOWAIT | __GFP_NOWARN);
}

static void __print_depot_stack(depot_stack_handle_t stack,
				char *buf, int sz, int indent)
{
	unsigned long *entries;
	unsigned int nr_entries;

	nr_entries = stack_depot_fetch(stack, &entries);
	stack_trace_snprint(buf, sz, entries, nr_entries, indent);
}

static int cmphandle(const void *_a, const void *_b)
{
	const depot_stack_handle_t * const a = _a, * const b = _b;

	if (*a < *b)
		return -1;
	else if (*a > *b)
		return 1;
	else
		return 0;
}

void
__intel_wakeref_tracker_show(const struct intel_wakeref_tracker *w,
			     struct drm_printer *p)
{
	unsigned long i;
	char *buf;

	buf = kmalloc(PAGE_SIZE, GFP_NOWAIT | __GFP_NOWARN);
	if (!buf)
		return;

	if (w->last_acquire) {
		__print_depot_stack(w->last_acquire, buf, PAGE_SIZE, 2);
		drm_printf(p, "Wakeref last acquired:\n%s", buf);
	}

	if (w->last_release) {
		__print_depot_stack(w->last_release, buf, PAGE_SIZE, 2);
		drm_printf(p, "Wakeref last released:\n%s", buf);
	}

	drm_printf(p, "Wakeref count: %lu\n", w->count);

	sort(w->owners, w->count, sizeof(*w->owners), cmphandle, NULL);

	for (i = 0; i < w->count; i++) {
		depot_stack_handle_t stack = w->owners[i];
		unsigned long rep;

		rep = 1;
		while (i + 1 < w->count && w->owners[i + 1] == stack)
			rep++, i++;
		__print_depot_stack(stack, buf, PAGE_SIZE, 2);
		drm_printf(p, "Wakeref x%lu taken at:\n%s", rep, buf);
	}

	kfree(buf);
}

void intel_wakeref_tracker_show(struct intel_wakeref_tracker *w,
				struct drm_printer *p)
{
	struct intel_wakeref_tracker tmp = {};

	do {
		unsigned long alloc = tmp.count;
		depot_stack_handle_t *s;

		spin_lock_irq(&w->lock);
		tmp.count = w->count;
		if (tmp.count <= alloc)
			memcpy(tmp.owners, w->owners, tmp.count * sizeof(*s));
		tmp.last_acquire = w->last_acquire;
		tmp.last_release = w->last_release;
		spin_unlock_irq(&w->lock);
		if (tmp.count <= alloc)
			break;

		s = krealloc(tmp.owners,
			     tmp.count * sizeof(*s),
			     GFP_NOWAIT | __GFP_NOWARN);
		if (!s)
			goto out;

		tmp.owners = s;
	} while (1);

	__intel_wakeref_tracker_show(&tmp, p);

out:
	intel_wakeref_tracker_fini(&tmp);
}

intel_wakeref_t intel_wakeref_tracker_add(struct intel_wakeref_tracker *w)
{
	depot_stack_handle_t stack, *stacks;
	unsigned long flags;

	stack = __save_depot_stack();
	if (!stack)
		return -1;

	spin_lock_irqsave(&w->lock, flags);

	if (!w->count)
		w->last_acquire = stack;

	stacks = krealloc(w->owners,
			  (w->count + 1) * sizeof(*stacks),
			  GFP_NOWAIT | __GFP_NOWARN);
	if (stacks) {
		stacks[w->count++] = stack;
		w->owners = stacks;
	} else {
		stack = -1;
	}

	spin_unlock_irqrestore(&w->lock, flags);

	return stack;
}

void intel_wakeref_tracker_remove(struct intel_wakeref_tracker *w,
				  intel_wakeref_t stack)
{
	unsigned long flags, n;
	bool found = false;

	if (unlikely(stack == -1))
		return;

	spin_lock_irqsave(&w->lock, flags);
	for (n = w->count; n--; ) {
		if (w->owners[n] == stack) {
			memmove(w->owners + n,
				w->owners + n + 1,
				(--w->count - n) * sizeof(stack));
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&w->lock, flags);

	if (WARN(!found,
		 "Unmatched wakeref %x, tracking %lu\n",
		 stack, w->count)) {
		char *buf;

		buf = kmalloc(PAGE_SIZE, GFP_NOWAIT | __GFP_NOWARN);
		if (!buf)
			return;

		__print_depot_stack(stack, buf, PAGE_SIZE, 2);
		pr_err("wakeref %x from\n%s", stack, buf);

		stack = READ_ONCE(w->last_release);
		if (stack && !w->count) {
			__print_depot_stack(stack, buf, PAGE_SIZE, 2);
			pr_err("wakeref last released at\n%s", buf);
		}

		kfree(buf);
	}
}

struct intel_wakeref_tracker
__intel_wakeref_tracker_reset(struct intel_wakeref_tracker *w)
{
	struct intel_wakeref_tracker saved;

	lockdep_assert_held(&w->lock);

	saved = *w;

	w->owners = NULL;
	w->count = 0;
	w->last_release = __save_depot_stack();

	return saved;
}

void intel_wakeref_tracker_reset(struct intel_wakeref_tracker *w,
				 struct drm_printer *p)
{
	struct intel_wakeref_tracker tmp;

	spin_lock_irq(&w->lock);
	tmp = __intel_wakeref_tracker_reset(w);
	spin_unlock_irq(&w->lock);

	if (tmp.count)
		__intel_wakeref_tracker_show(&tmp, p);

	intel_wakeref_tracker_fini(&tmp);
}

void intel_wakeref_tracker_init(struct intel_wakeref_tracker *w)
{
	memset(w, 0, sizeof(*w));
	spin_lock_init(&w->lock);
}

void intel_wakeref_tracker_fini(struct intel_wakeref_tracker *w)
{
	kfree(w->owners);
}
