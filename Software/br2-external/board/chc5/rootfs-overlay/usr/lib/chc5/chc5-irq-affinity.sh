#!/bin/sh
# SPDX-License-Identifier: CC-BY-NC-ND-4.0
#
# chc5-irq-affinity.sh - put the latency-critical interrupts on CPU1, once at boot
#
# Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
# https://creativecommons.org/licenses/by-nc-nd/4.0/

CPU1_MASK=2

udevadm settle --timeout=30 ||
    logger -t chc5-irq-affinity "udev not settled after 30 s -- placing anyway"

irq=$(awk '{ for (i = 2; i < NF; i++) if ($i == "GIC-0") {
                 if ($(i + 1) == 80) { sub(":", "", $1); print $1; exit }
                 break } }' /proc/interrupts)
if [ -n "$irq" ] && echo $CPU1_MASK > /proc/irq/"$irq"/smp_affinity; then
    logger -t chc5-irq-affinity "IRQ $irq (GIC 80, FX3 mailbox i2c-1) -> CPU1"
else
    logger -t chc5-irq-affinity "i2c-1 (GIC 80) not found -- left where it is"
fi

if echo $CPU1_MASK > /proc/irq/default_smp_affinity; then
    logger -t chc5-irq-affinity "interrupts requested from now on start on CPU1 (GVSP frame done, GIC 64)"
fi
exit 0
