/*
 * chc5_ptp_timebase.v - PTP time base and frame sequencer for chc5_camio_sync
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module chc5_ptp_timebase #(
    parameter [47:0] INC_FX   = 48'h6_AAAA_AAAB,
    parameter integer GUARD_NS = 1000,
    parameter integer EVT_MINW = 4,
    parameter integer EVT_MINP = 1000,
    parameter integer SETTLE   = 4,
    parameter integer LINE_MIN = 270
)(
    input  wire        clk,
    input  wire        rstn,

    input  wire        tb_en,
    input  wire        evt_en,
    input  wire        sched_en,
    input  wire        load_pulse,
    input  wire [63:0] load_val,
    input  wire        commit_pulse,
    input  wire [31:0] rate_adj,
    input  wire        evt_arm,
    input  wire [63:0] evt_next_val,
    input  wire [31:0] evt_period,
    input  wire [7:0]  evt_width,
    input  wire        evt_pol,
    input  wire [63:0] sched_next,
    input  wire [63:0] sched_period,
    input  wire [31:0] sched_period_frac,
    input  wire [23:0] line_period,
    input  wire [23:0] frame_lines,
    output wire        commit_busy,
    input  wire        cap_en,
    input  wire [3:0]  cap_tag,
    input  wire        cap_latch,
    input  wire        cap_pop,

    output reg         sched_fire,
    output wire        ptp_active,
    output wire [23:0] ln_period,
    output wire [23:0] ln_period_eff,
    output wire [23:0] ln_count,
    output wire [27:0] step12,
    output wire        ln_more,
    input  wire        cap_evt,
    output reg         evt_fire,

    output wire        ptp_evt_o,
    output wire        ptp_evt_t,

    output wire [63:0] time_now,
    output wire [31:0] ptp_status,
    output wire [31:0] evt_count,
    output wire [31:0] cap_head_lo,
    output wire [31:0] cap_hold_hi,
    output wire [31:0] cap_hold_meta,
    output wire [31:0] cap_status
);
    localparam [2:0] S_IDLE = 3'd0, S_WAIT = 3'd1, S_RUN = 3'd2, S_REANC = 3'd3, S_STOP = 3'd4;

    reg [47:0] step;
    always @(posedge clk)
        if (!rstn) step <= INC_FX;
        else       step <= INC_FX + {{16{rate_adj[31]}}, rate_adj};
    wire [15:0] step_ns   = step[47:32];
    wire [31:0] step_frac = step[31:0];

    (* max_fanout = 48 *) reg load_q;
    reg  [63:0] load_val_q;
    always @(posedge clk) begin
        load_q     <= rstn & load_pulse;
        load_val_q <= load_val;
    end

    reg  [31:0] frac;   reg c_frac;
    reg  [31:0] t_lo, t_hi, t_hi1;  reg c_tlo;
    reg         never_disc;

    wire [32:0] frac_sum = {1'b0, frac} + {1'b0, step_frac};
    wire [32:0] tlo_sum  = {1'b0, t_lo} + {17'd0, step_ns} + {32'd0, c_frac};

    always @(posedge clk) begin
        if (!rstn) begin
            frac <= 32'd0; c_frac <= 1'b0;
            t_lo <= 32'd0; t_hi <= 32'd0; t_hi1 <= 32'd1; c_tlo <= 1'b0;
            never_disc <= 1'b1;
        end else begin
            if (tb_en) begin
                frac  <= frac_sum[31:0];  c_frac <= frac_sum[32];
            end
            if (load_q) begin
                t_lo <= load_val_q[31:0]; t_hi <= load_val_q[63:32]; t_hi1 <= load_val_q[63:32] + 32'd1;
                c_tlo <= 1'b0; never_disc <= 1'b0;
            end else if (tb_en) begin
                t_lo  <= tlo_sum[31:0];   c_tlo  <= tlo_sum[32];
                t_hi  <= t_hi + c_tlo;
                t_hi1 <= c_tlo ? t_hi + 32'd2 : t_hi + 32'd1;
            end
        end
    end

    assign time_now = {(c_tlo ? t_hi1 : t_hi), t_lo};
    assign step12   = step[47:20] + {27'd0, |step[19:0]};

    reg  [63:0] ev_next, ev_d;
    reg         ev_armed, ev_pulse, ev_refused;
    reg  [1:0]  ev_chk;
    reg  [31:0] ev_cnt;
    reg  [7:0]  ev_wcnt;
    reg  [2:0]  ev_settle;
    wire [7:0]  ev_w = (evt_width < EVT_MINW) ? EVT_MINW[7:0] : evt_width;

    always @(posedge clk) ev_d <= time_now - ev_next;
    wire ev_due   = ~ev_d[63] & (|ev_d[62:0]);
    wire ev_late  = ~ev_d[63] & (ev_d[62:0] >= GUARD_NS);
    wire ev_ahead = $signed(ev_d) < -$signed(GUARD_NS);
    wire ev_per_ok = (evt_period == 32'd0) || (evt_period >= EVT_MINP);

    always @(posedge clk) begin
        if (!rstn) begin
            ev_next <= 64'd0; ev_armed <= 1'b0; ev_pulse <= 1'b0; ev_wcnt <= 8'd0; ev_chk <= 2'd0;
            ev_settle <= 3'd0; ev_cnt <= 32'd0; ev_refused <= 1'b0; evt_fire <= 1'b0;
        end else if (!tb_en) begin
            ev_armed <= 1'b0; ev_pulse <= 1'b0; ev_wcnt <= 8'd0; ev_settle <= 3'd0; ev_chk <= 2'd0;
            evt_fire <= 1'b0;
            if (evt_arm) ev_refused <= 1'b1;
        end else begin
            evt_fire <= 1'b0;
            if (ev_settle != 0) ev_settle <= ev_settle - 1'b1;
            if (ev_pulse) begin
                if (ev_wcnt <= 8'd1) ev_pulse <= 1'b0;
                else                 ev_wcnt  <= ev_wcnt - 1'b1;
            end
            if (evt_arm) begin
                if (ev_per_ok) begin
                    ev_next <= evt_next_val; ev_armed <= 1'b1; ev_chk <= 2'd2;
                    ev_settle <= SETTLE[2:0];
                end else begin
                    ev_armed <= 1'b0; ev_chk <= 2'd0; ev_refused <= 1'b1;
                end
            end else if (load_q) begin
                ev_armed <= 1'b0; ev_chk <= 2'd0;
            end else if (ev_chk == 2'd2) begin
                ev_chk <= 2'd1;
            end else if (ev_chk == 2'd1) begin
                ev_chk <= 2'd0;
                if (ev_ahead) begin ev_cnt <= 32'd0; ev_refused <= 1'b0; end
                else          begin ev_armed <= 1'b0; ev_refused <= 1'b1; end
            end else if (evt_en && ev_armed && ev_due && ev_settle == 0 && !ev_pulse && ev_late) begin
                ev_armed <= 1'b0; ev_refused <= 1'b1;
            end else if (evt_en && ev_armed && ev_due && ev_settle == 0 && !ev_pulse) begin
                ev_pulse <= 1'b1; ev_wcnt <= ev_w; ev_cnt <= ev_cnt + 1'b1; ev_settle <= SETTLE[2:0];
                evt_fire <= 1'b1;
                if (evt_period == 32'd0) ev_armed <= 1'b0;
                else                     ev_next  <= ev_next + {32'd0, evt_period};
            end
        end
    end

    assign ptp_evt_o = ~evt_pol;
    assign ptp_evt_t = ~ev_pulse;
    assign evt_count = ev_cnt;

    localparam integer NS_W = 48;
    localparam integer RW   = NS_W + 1 + 32;
    localparam [RW-1:0] ONE_LSB = {{(RW-1){1'b0}}, 1'b1};
    wire [RW-1:0] step_r = {{(RW-48){1'b0}}, step};

    reg  [RW-1:0] rem;
    reg  [RW-1:0] p_fx, pp_fx;
    reg  [23:0]   l_ns, n_act, pl_ns, pn;
    reg  [23:0]   l_eff_ns, pl_eff_ns;
    reg  [2:0]    st;
    reg           tgt_ok;
    reg           late;

    wire running  = (st == S_RUN) || (st == S_REANC) || (st == S_STOP);
    wire counting = running || tgt_ok;
    wire due      = rem[RW-1];
    wire ev_start = (st == S_WAIT) && tgt_ok && due;
    wire ev_frame = running && due;
    wire frame_evt = ev_start || ev_frame;
    wire [RW-1:0] p_add = (st == S_REANC) ? pp_fx : p_fx;

    reg  [RW-1:0] p_add_r;
    always @(posedge clk) p_add_r <= p_add;
    (* keep = "true" *) wire [RW-1:0] rem_tick = rem - step_r;
    (* keep = "true" *) wire [RW-1:0] rem_frm  = rem - step_r + p_add_r;
    wire add_p = frame_evt && !(st == S_STOP);

    reg  [1:0]    cm;
    reg           cm_evt, cm_req, commit_q;
    reg  [1:0]    ld_settle;
    reg  [RW-1:0] d;
    reg           rng_ok, chk_fut, chk_long, set_ok, p_lt;
    always @(posedge clk) commit_q <= rstn & commit_pulse;
    wire          ld_busy  = load_pulse | load_q | (ld_settle != 2'd0);
    wire          cm_start = (cm == 2'd0) && cm_req && !ld_busy;
    assign commit_busy = commit_pulse | commit_q | cm_req | (cm != 2'd0);

    wire [63:0]   diff64  = sched_next - time_now - {63'd0, c_frac};
    wire [23:0]   l_floor = (line_period < LINE_MIN[23:0]) ? LINE_MIN[23:0] : line_period;
    wire [RW-1:0] two_l   = {{(RW-57){1'b0}}, l_floor, 1'b0, 32'd0};
    wire [RW-1:0] p_in    = {1'b0, sched_period[NS_W-1:0], sched_period_frac};
    wire [RW-1:0] p_new   = p_lt ? two_l : p_in;
    wire [RW-1:0] d_m     = d - step_r;

    always @(posedge clk) begin
        if (!rstn || !tb_en) begin
            st <= S_IDLE; tgt_ok <= 1'b0; rem <= {RW{1'b0}};
            sched_fire <= 1'b0; cm <= 2'd0; cm_evt <= 1'b0; cm_req <= 1'b0; ld_settle <= 2'd0;
            d <= {RW{1'b0}}; rng_ok <= 1'b0; chk_fut <= 1'b0; chk_long <= 1'b0; set_ok <= 1'b0; p_lt <= 1'b0;
            if (!rstn) begin
                late <= 1'b0;
                p_fx <= {RW{1'b0}}; l_ns <= 24'd0; n_act <= 24'd0;
                pp_fx <= {RW{1'b0}}; pl_ns <= 24'd0; pn <= 24'd0;
                l_eff_ns <= LINE_MIN[23:0]; pl_eff_ns <= LINE_MIN[23:0];
            end else if (commit_q) begin
                late <= 1'b1;
            end
        end else begin
            sched_fire <= 1'b0;
            if (load_q)              ld_settle <= 2'd3;
            else if (ld_settle != 0) ld_settle <= ld_settle - 1'b1;
            cm_req <= commit_q | (cm_req & ~cm_start);

            if (counting) rem <= add_p ? rem_frm : rem_tick;
            if (ev_start) begin
                sched_fire <= 1'b1; st <= S_RUN;
            end else if (ev_frame) begin
                if (st == S_STOP) begin
                    st <= S_IDLE; tgt_ok <= 1'b0;
                end else if (st == S_REANC) begin
                    sched_fire <= 1'b1; st <= S_RUN;
                    p_fx <= pp_fx; l_ns <= pl_ns; n_act <= pn; l_eff_ns <= pl_eff_ns;
                end else begin
                    sched_fire <= 1'b1;
                end
            end else begin
                case (st)
                    S_IDLE: begin
                        if (sched_en) st <= S_WAIT;
                        else if (tgt_ok && due) begin
                            tgt_ok <= 1'b0; late <= 1'b1;
                        end
                    end
                    S_WAIT:  if (!sched_en) st <= S_IDLE;
                    S_RUN,
                    S_REANC: if (!sched_en) st <= S_STOP;
                    default: ;
                endcase
            end

            if (cm != 2'd0) d <= d_m;
            case (cm)
                2'd0: begin
                    cm_evt <= 1'b0;
                    if (cm_start) cm <= 2'd1;
                end
                2'd1: begin
                    d      <= {diff64[RW-33:0], 32'd0} - step_r - ONE_LSB;
                    rng_ok <= (diff64[63:NS_W] == {(64-NS_W){1'b0}});
                    p_lt   <= (sched_period[NS_W-1:0] < {{(NS_W-25){1'b0}}, l_floor, 1'b0});
                    set_ok <= (line_period != 24'd0) && (sched_period[63:NS_W] == {(64-NS_W){1'b0}});
                    cm <= 2'd2; cm_evt <= frame_evt;
                end
                2'd2: begin
                    chk_fut  <= rng_ok && !d[RW-1] && (d[RW-2:32] >= GUARD_NS);
                    chk_long <= ($signed(d[RW-1:32]) >= $signed(rem[RW-1:32]) - 1);
                    cm <= 2'd3; cm_evt <= cm_evt | frame_evt;
                end
                default: begin
                    if (cm_evt || frame_evt) begin
                        cm <= 2'd2; cm_evt <= 1'b0;
                    end else begin
                        cm <= 2'd0;
                        if (st == S_IDLE || st == S_WAIT) begin
                            p_fx <= p_new; l_ns <= line_period; n_act <= frame_lines; l_eff_ns <= l_floor;
                            rem  <= d_m;
                            tgt_ok <= chk_fut && set_ok; late <= ~(chk_fut && set_ok);
                        end else if ((st == S_RUN || st == S_REANC) && chk_fut && chk_long && set_ok) begin
                            pp_fx <= p_new; pl_ns <= line_period; pn <= frame_lines; pl_eff_ns <= l_floor;
                            rem  <= d_m; st <= S_REANC; late <= 1'b0;
                        end else begin
                            late <= 1'b1;
                        end
                    end
                end
            endcase
        end
    end

    assign ln_period     = l_ns;
    assign ln_period_eff = l_eff_ns;
    assign ln_count  = n_act;
    reg ln_more_r;
    always @(posedge clk)
        ln_more_r <= rstn && (st == S_REANC) && !rem[RW-1] &&
                     (rem[RW-2:32] >= {{(NS_W-24){1'b0}}, l_ns} + {{(NS_W-16){1'b0}}, step_ns} + 1);
    assign ln_more   = ln_more_r;

    assign ptp_active = running;

    reg  [95:0] fifo [0:7];
    reg  [2:0]  wp, rp;
    reg  [3:0]  lvl;
    reg  [23:0] cap_num, cap_all;
    reg         cap_en_d, ovr_pend, ovr_sticky;
    reg  [31:0] hold_hi, hold_meta;
    reg         hold_valid;

    wire        f_empty = (lvl == 4'd0);
    wire        f_full  = (lvl == 4'd8);
    wire        do_push = cap_en && cap_evt && !f_full;
    wire        do_pop  = cap_pop && hold_valid && !f_empty;
    wire [95:0] head    = fifo[rp];

    always @(posedge clk) begin
        if (do_push) fifo[wp] <= {1'b1, 2'b00, ovr_pend, cap_tag, cap_num, time_now};
    end

    always @(posedge clk) begin
        if (!rstn) begin
            wp <= 3'd0; rp <= 3'd0; lvl <= 4'd0; cap_num <= 24'd0; cap_all <= 24'd0;
            cap_en_d <= 1'b0; ovr_pend <= 1'b0; ovr_sticky <= 1'b0;
            hold_hi <= 32'd0; hold_meta <= 32'd0; hold_valid <= 1'b0;
        end else begin
            cap_en_d <= cap_en;
            if (cap_evt) cap_all <= cap_all + 1'b1;
            if (cap_en && !cap_en_d) begin
                wp <= 3'd0; rp <= 3'd0; lvl <= 4'd0; cap_num <= 24'd0;
                ovr_pend <= 1'b0; ovr_sticky <= 1'b0; hold_valid <= 1'b0;
            end else begin
                if (cap_en && cap_evt) begin
                    cap_num <= cap_num + 1'b1;
                    if (f_full) begin ovr_pend <= 1'b1; ovr_sticky <= 1'b1; end
                    else begin wp <= wp + 1'b1; ovr_pend <= 1'b0; end
                end
                if (do_pop) begin rp <= rp + 1'b1; hold_valid <= 1'b0; end
                lvl <= lvl + {3'd0, do_push} - {3'd0, do_pop};
                if (cap_latch) begin
                    hold_hi    <= f_empty ? 32'd0 : head[63:32];
                    hold_meta  <= f_empty ? 32'd0 : head[95:64];
                    hold_valid <= !f_empty;
                end
            end
        end
    end

    assign cap_head_lo   = f_empty ? 32'd0 : head[31:0];
    assign cap_hold_hi   = hold_hi;
    assign cap_hold_meta = hold_meta;
    assign cap_status    = {cap_all, 2'b00, ovr_sticky, ~f_empty, lvl};

    assign ptp_status = {25'd0, ev_refused, (st == S_WAIT || st == S_REANC), late, never_disc, running, 2'b00};

endmodule
