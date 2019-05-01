#!/bin/bash

sudo rmmod memga-4.ko
make clean
make
sudo insmod memga-4.ko

