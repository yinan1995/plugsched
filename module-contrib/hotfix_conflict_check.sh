#!/bin/bash
# Copyright 2019-2022 Alibaba Group Holding Limited.
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

# input file format:
#	function sympos module
#
# valid e.g:
#	pick_next_task 1 vmlinux
#	ext4_free_blocks 2 ext4

if [ "$1" == "" ]; then
	echo Error: please input files!
	exit 1
elif [ ! -e "$1" ]; then
	echo Error: input file is not exist!
	exit 1
else
	tainted_file=$1
fi

func_list=$(mktemp)

# Some hotfix do not provide the sympos of patched function, so use a new set
func_list_nosympos=$(mktemp)

trap "rm -r $func_list $func_list_nosympos" INT HUP QUIT ABRT ALRM TERM EXIT # ensures it is deleted when script ends

# deal with kpatch prev-0.4 ABI
find /sys/kernel/kpatch/patches/*/functions -type d -not -path "*/functions" 2>/dev/null | while read path ; do
	# /sys/kernel/kpatch/patches/kpatch_D689377/functions/blk_mq_update_queue_map -> blk_mq_update_queue_map
	func="${path##*/}"
	echo "$func" >> $func_list_nosympos
done

# deal with kpatch 0.4 ABI, livepatch and plugsched
for subdir in kpatch livepatch plugsched; do
	find /sys/kernel/$subdir/*/ -type d -path "*,[0-9]" 2>/dev/null | while read path ; do
		# /sys/kernel/kpatch/kpatch_5135717/vmlinux/kernfs_find_ns,1 -> kernfs_find_ns,1
		func_ver=`echo $path | awk -F / -e '{print $NF}'`
		mod=`echo $path | awk -F / -e '{print $(NF-1)}'`
		func=`echo $func_ver | awk -F , '{print $1}'`
		ver=`echo $func_ver | awk -F , '{print $2}'`
		echo "$func $ver $mod" >> $func_list
	done
done

# deal with manual hotfix that has sys directory entry
find /sys/kernel/manual_*/ -type d -not -path "*manual_*/" 2>/dev/null | while read path ; do
	func="${path##*/}"
	echo "$func" >> $func_list_nosympos
done

# deal with manual hotfix that does not have sys directory entry, i.e, the early days implemenation
for func in `cat /proc/kallsyms | grep '\[kpatch_' | grep -v __kpatch | awk '{print $3}' | grep -v 'patch_'`; do
	if [ $(grep "e9_$func" /proc/kallsyms | wc -l) -gt 0 ]; then
		echo "$func" >> $func_list_nosympos
	fi
done

if [ "$(awk 'END{print NF}' $tainted_file)" != "3" ]; then
	# tainted_file provided by manual_hotfix or kpatch-pre-0.4 that don't have the sympos
	conflicts=$(sort <(awk '{print $1}' $tainted_file) <(awk '{print $1}' $func_list) | uniq -d)
else
	# Get the conflict functions
	conflicts=$(sort $tainted_file <(awk '{print $1" "$2" "$3}' $func_list) | uniq -d)
fi

conflicts_nosympos=$(sort <(awk '{print $1}' $tainted_file) <(awk '{print $1}' $func_list_nosympos) | uniq -d)

if [ "$conflicts" != "" -o "$conflicts_nosympos" != "" ]; then
	echo Error: confict detected:
	if [ "$conflicts" != "" ]; then
		echo $(awk '{print $1}' <(echo $conflicts))
	elif [ "$conflicts_nosympos" != "" ]; then
		echo $conflicts_nosympos
	fi
	exit 1
fi
