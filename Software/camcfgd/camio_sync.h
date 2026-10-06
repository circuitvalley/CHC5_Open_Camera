/* SPDX-License-Identifier: CC-BY-NC-ND-4.0 */
/*
 * camio_sync.h - camcfgd client for the chc5_camio_sync aux-I/O + sync block
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

#ifndef CAMCFGD_CAMIO_SYNC_H
#define CAMCFGD_CAMIO_SYNC_H

#include <stdbool.h>
#include <stdint.h>
#include "chc5_camio.h"

int  camio_sync_open(void);
bool camio_sync_present(void);
void camio_sync_close(void);

int  camio_sync_hw_state(void);
int  camio_sync_has_ptp_pulse(void);

int  camio_sync_apply(const struct camio_cfg *cfg);

#define CHC5_FACT_UNKNOWN  0xFF
#define CHC5_FACT_NONE     0xFE
struct camio_facts {
	int      has_trig;
	int      can_trigger;
	int      can_follow;
	int      follow_xhs;
	uint32_t line_ps;
	uint32_t frame_lines;
	uint32_t min_frame_ns;
	unsigned role_by;
	unsigned trig_route;
	int      has_strobe;
	int      strobe_low;
	int      xvs_out;
};
int  camio_sync_resolve(const struct camio_cfg *in, const struct camio_facts *f,
                        struct camio_cfg *out, const char **why);
int  camio_sync_set_sensor(const struct camio_facts *f);
uint32_t camio_sync_check(const struct camio_cfg *c, uint32_t mask);
int  camio_sync_plan_follower(const struct camio_cfg *c);
int  camio_sync_wants_follower(void);
uint32_t camio_sync_follow_rate_mhz(void);
uint32_t camio_sync_io_caps(void);
int  camio_sync_fire(int ev);
uint32_t camio_sync_sched_rate_mhz(void);
int  camio_sync_source(void);
const char *camio_sync_source_name(int src);
int  camio_sync_uses_xtrig(void);
int  camio_sync_sched_status(uint32_t *st);
int  camio_sync_line_levels(uint32_t *levels);
void camio_sync_sensor_running(int on);

int  camio_sync_read_status(uint32_t *status, uint32_t *in_status,
                            uint32_t *sync_status);

int  camio_sync_has_trigger(void);
void     camio_sync_note_change(void);
unsigned camio_sync_change_gen(void);
void     camio_sync_applied(unsigned gen);
int  camio_sync_software_trigger(void);
int  camio_sync_trigger_count(uint64_t *count);
void camio_sync_set_width_auto(int on);
void camio_sync_set_exposure(uint32_t exposure_us, uint32_t offset_ns);

#endif
