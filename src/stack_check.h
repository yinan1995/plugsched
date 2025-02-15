// Copyright 2019-2022 Alibaba Group Holding Limited.
// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

#include <linux/list.h>
#include <trace/events/sched.h>

#define MAX_STACK_ENTRIES	100

extern const char *get_ksymbol(struct module *, unsigned long,
		unsigned long *, unsigned long *);

extern int process_id[];

static inline void addr_swap(unsigned long *a, unsigned long *b)
{
	if (*a ^ *b) {
		*a = *a ^ *b;
		*b = *b ^ *a;
		*a = *a ^ *b;
	}
}

/* This sort method is coming from lib/sort.c */
void addr_sort(unsigned long *addr, unsigned long *size, int n) {
	int i = n/2 - 1, c, r;

	for ( ; i >= 0; i -= 1) {
		for (r = i; r * 2 + 1 < n; r  = c) {
			c = r * 2 + 1;
			if (c < n - 1 &&
					*(addr + c) < *(addr + c + 1))
				c += 1;
			if (*(addr + r) >= *(addr + c))
				break;
			addr_swap(addr + r, addr + c);
			addr_swap(size + r, size + c);
		}
	}

	for (i = n - 1; i > 0; i -= 1) {
		addr_swap(addr, addr + i);
		addr_swap(size, size + i);
		for (r = 0; r * 2 + 1 < i; r = c) {
			c = r * 2 + 1;
			if (c < i - 1 &&
					*(addr + c) < *(addr + c + 1))
				c += 1;
			if (*(addr + r) >= *(addr + c))
				break;
			addr_swap(addr + r, addr + c);
			addr_swap(size + r, size + c);
		}
	}
}

static void stack_check_init(void)
{
	#define PLUGSCHED_FN_PTR EXPORT_PLUGSCHED
	#define EXPORT_PLUGSCHED(fn, ...) 				\
		kallsyms_lookup_size_offset(orig_##fn, 			\
				&orig_##fn##_size, NULL); 		\
		vm_func_size[NR_##fn] = orig_##fn##_size;

	#include "export_jump.h"
	#undef EXPORT_PLUGSCHED
	#undef PLUGSCHED_FN_PTR

	addr_sort(vm_func_addr, vm_func_size, NR_INTERFACE_FN);

	#define PLUGSCHED_FN_PTR(fn, ...) 				\
		get_ksymbol(THIS_MODULE,(unsigned long)__mod_##fn, 	\
				&mod_##fn##_size, NULL); 		\
		mod_func_size[NR_##fn] = mod_##fn##_size;

	#define EXPORT_PLUGSCHED(fn, ...) 				\
		get_ksymbol(THIS_MODULE,(unsigned long)fn, 		\
				&mod_##fn##_size, NULL); 		\
		mod_func_size[NR_##fn] = mod_##fn##_size;

	#include "export_jump.h"
	#undef EXPORT_PLUGSCHED
	#undef PLUGSCHED_FN_PTR

	addr_sort(mod_func_addr, mod_func_size, NR_INTERFACE_FN);
}

static int heavy_stack_check_fn_insmod(struct stack_trace *trace)
{
	unsigned long address;
	int i, idx;

	for (i = 0; i < trace->nr_entries; i++) {
		address = trace->entries[i];
		idx = bsearch(vm_func_addr, 0, NR_INTERFACE_FN - 1, address);
		if (idx == -1)
			continue;

		if (address < vm_func_addr[idx] + vm_func_size[idx])
			return -EAGAIN;
	}

	return 0;
}

static int heavy_stack_check_fn_rmmod(struct stack_trace *trace)
{
	unsigned long address;
	int i, idx;

	for (i = 0; i < trace->nr_entries; i++) {
		address = trace->entries[i];
		idx = bsearch(mod_func_addr, 0, NR_INTERFACE_FN - 1, address);
		if (idx == -1)
			continue;

		if (address < mod_func_addr[idx] + mod_func_size[idx])
			return -EAGAIN;
	}

	return 0;
}

/* This is basically copied from klp_check_stack */
static int stack_check_task(struct task_struct *task, bool install)
{
	unsigned long entries[MAX_STACK_ENTRIES];
	struct stack_trace trace;

	trace.skip = 0;
	trace.nr_entries = 0;
	trace.max_entries = MAX_STACK_ENTRIES;
	trace.entries = entries;

	save_stack_trace_tsk(task, &trace);

	if (install)
		return heavy_stack_check_fn_insmod(&trace);
	else
		return heavy_stack_check_fn_rmmod(&trace);
}

static int stack_check(bool install)
{
	struct task_struct *p, *t;
	int task_count = 0;
	int nr_cpus = num_online_cpus();
	int cpu = smp_processor_id();

	for_each_process_thread(p, t) {
		if ((task_count % nr_cpus) == process_id[cpu]) {
			if (stack_check_task(t, install))
				return -EBUSY;
		}
		task_count++;
	}

	t = idle_task(cpu);
	if (stack_check_task(t, install))
		return -EBUSY;

	return 0;
}
