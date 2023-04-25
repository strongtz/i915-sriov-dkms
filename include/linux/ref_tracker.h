// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _LINUX_REF_TRACKER_H
#define _LINUX_REF_TRACKER_H
#include <linux/refcount.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/stackdepot.h>

struct ref_tracker;

struct ref_tracker_dir {
#ifdef CONFIG_REF_TRACKER
	spinlock_t		lock;
	unsigned int		quarantine_avail;
	refcount_t		untracked;
	refcount_t		no_tracker;
	bool			dead;
	struct list_head	list; /* List of active trackers */
	struct list_head	quarantine; /* List of dead trackers */
	char			name[32];
#endif
};

#ifdef CONFIG_REF_TRACKER

// Temporary allow two and three arguments, until consumers are converted
#define ref_tracker_dir_init(_d, _q, args...) _ref_tracker_dir_init(_d, _q, ##args, #_d)
#define _ref_tracker_dir_init(_d, _q, _n, ...) __ref_tracker_dir_init(_d, _q, _n)

static inline void __ref_tracker_dir_init(struct ref_tracker_dir *dir,
					unsigned int quarantine_count,
					const char *name)
{
	INIT_LIST_HEAD(&dir->list);
	INIT_LIST_HEAD(&dir->quarantine);
	spin_lock_init(&dir->lock);
	dir->quarantine_avail = quarantine_count;
	dir->dead = false;
	refcount_set(&dir->untracked, 1);
	refcount_set(&dir->no_tracker, 1);
	strlcpy(dir->name, name, sizeof(dir->name));
	stack_depot_init();
}

void ref_tracker_dir_exit(struct ref_tracker_dir *dir);

void __ref_tracker_dir_print(struct ref_tracker_dir *dir,
			   unsigned int display_limit);

void ref_tracker_dir_print(struct ref_tracker_dir *dir,
			   unsigned int display_limit);

//int ref_tracker_dir_snprint(struct ref_tracker_dir *dir, char *buf, size_t size);
struct ostream {
	char *buf;
	int size, used;
};

#define pr_ostream(stream, fmt, args...) \
({ \
	struct ostream *_s = (stream); \
\
	if (!_s->buf) { \
		pr_err(fmt, ##args); \
	} else { \
		int ret, len = _s->size - _s->used; \
		ret = snprintf(_s->buf + _s->used, len, pr_fmt(fmt), ##args); \
		_s->used += min(ret, len); \
	} \
})

static void
__ref_tracker_dir_pr_ostream(struct ref_tracker_dir *dir,
			     unsigned int display_limit, struct ostream *s)
{
	struct ref_tracker_dir_stats *stats;
	unsigned int i = 0, skipped;
	depot_stack_handle_t stack;
	char *sbuf;

	lockdep_assert_held(&dir->lock);

	if (list_empty(&dir->list))
		return;

	stats = ref_tracker_get_stats(dir, display_limit);
	if (IS_ERR(stats)) {
		pr_ostream(s, "%s@%pK: couldn't get stats, error %pe\n",
			   dir->name, dir, stats);
		return;
	}

	sbuf = kmalloc(STACK_BUF_SIZE, GFP_NOWAIT | __GFP_NOWARN);

	for (i = 0, skipped = stats->total; i < stats->count; ++i) {
		stack = stats->stacks[i].stack_handle;
		if (sbuf && !stack_depot_snprint(stack, sbuf, STACK_BUF_SIZE, 4))
			sbuf[0] = 0;
		pr_ostream(s, "%s@%pK has %d/%d users at\n%s\n", dir->name, dir,
			   stats->stacks[i].count, stats->total, sbuf);
		skipped -= stats->stacks[i].count;
	}

	if (skipped)
		pr_ostream(s, "%s@%pK skipped reports about %d/%d users.\n",
			   dir->name, dir, skipped, stats->total);

	kfree(sbuf);

	kfree(stats);
}
static int ref_tracker_dir_snprint(struct ref_tracker_dir *dir, char *buf, size_t size)
{
	struct ostream os = { .buf = buf, .size = size };
	unsigned long flags;

	spin_lock_irqsave(&dir->lock, flags);
	__ref_tracker_dir_pr_ostream(dir, 16, &os);
	spin_unlock_irqrestore(&dir->lock, flags);

	return os.used;
}


int ref_tracker_alloc(struct ref_tracker_dir *dir,
		      struct ref_tracker **trackerp, gfp_t gfp);

int ref_tracker_free(struct ref_tracker_dir *dir,
		     struct ref_tracker **trackerp);

#else /* CONFIG_REF_TRACKER */

static inline void ref_tracker_dir_init(struct ref_tracker_dir *dir,
					unsigned int quarantine_count,
					...)
{
}

static inline void ref_tracker_dir_exit(struct ref_tracker_dir *dir)
{
}

static inline void __ref_tracker_dir_print(struct ref_tracker_dir *dir,
					   unsigned int display_limit)
{
}

static inline void ref_tracker_dir_print(struct ref_tracker_dir *dir,
					 unsigned int display_limit)
{
}

static inline int ref_tracker_dir_snprint(struct ref_tracker_dir *dir,
					  char *buf, size_t size)
{
	return 0;
}

static inline int ref_tracker_alloc(struct ref_tracker_dir *dir,
				    struct ref_tracker **trackerp,
				    gfp_t gfp)
{
	return 0;
}

static inline int ref_tracker_free(struct ref_tracker_dir *dir,
				   struct ref_tracker **trackerp)
{
	return 0;
}

#endif

#endif /* _LINUX_REF_TRACKER_H */
