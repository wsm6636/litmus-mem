#!/bin/bash

sudo cat /sys/kernel/debug/tracing/trace > l.log
sudo cat /sys/kernel/debug/tracing/trace | grep 'overflow'> over.log
sudo cat /sys/kernel/debug/tracing/trace | grep 'ERR'> err.log
sudo cat /sys/kernel/debug/tracing/trace | grep 'throttle'> throttle.log
sudo cat /sys/kernel/debug/tracing/trace | grep 'perf_event_count' > perf.log
sudo cat /sys/kernel/debug/tracing/trace | grep 'master'> master.log
sudo cat /sys/kernel/debug/tracing/trace | grep 'count=' > count.log
sudo cat /sys/kernel/debug/tracing/trace | grep 'slave' > slave.log
sudo dmesg > log.txt

