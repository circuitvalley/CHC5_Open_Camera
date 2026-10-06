/*
 * camio_sync_core.v - camera aux I/O and multi-camera sync datapath
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module camio_sync_core #(
    parameter integer CLK_HZ   = 150_000_000,
    parameter integer TICK_NS  = 10_000,
    parameter integer DELAY_W  = 16,
    parameter integer TB_W     = 20,
    parameter integer DIV_W    = 8,
    parameter integer LOCK_W   = 14,
    parameter [47:0]  INC_FX   = 48'h6_AAAA_AAAB,
    parameter integer SYO_NS   = 100_000
)(
    input  wire               clk,
    input  wire               rstn,

    input  wire               enable,
    input  wire               soft_reset,
    input  wire               sw_trig,
    input  wire [1:0]         in_func,
    input  wire [1:0]         in_activation,
    input  wire               in_invert,
    input  wire [23:0]        in_filter,
    input  wire [DIV_W-1:0]   in_divider,
    input  wire [1:0]         trig_src,
    input  wire               width_mode,
    input  wire [30:0]        trig_delay,
    input  wire [30:0]        trig_width,
    input  wire [30:0]        trig_minper,
    input  wire               strobe_en,
    input  wire               strobe_invert,
    input  wire [1:0]         strobe_src,
    input  wire [DELAY_W-1:0] strobe_delay,
    input  wire [DELAY_W-1:0] strobe_dur,
    input  wire [DELAY_W-1:0] strobe_minon,
    input  wire [2:0]         out_src,
    input  wire               out_invert,
    input  wire               out_user_value,
    input  wire               native_low,
    input  wire [1:0]         sync_role,
    input  wire               xvs_pol,
    input  wire               xhs_pol,
    input  wire               role_by,
    input  wire [1:0]         sync_trig_route,
    input  wire               xhs_en,
    input  wire [23:0]        line_period,
    input  wire [23:0]        frame_lines,
    input  wire [2:0]         cap_src,
    input  wire               cap_edge,

    input  wire               sched_fire,
    input  wire               ptp_active,
    input  wire [23:0]        ln_period,
    input  wire [23:0]        ln_period_eff,
    input  wire [23:0]        ln_count,
    input  wire [27:0]        step12,
    input  wire               ln_more,
    input  wire               evt_fire,
    output wire               cap_evt,

    output wire [31:0]        status,
    output wire [31:0]        in_status,
    output wire [31:0]        sync_status,
    output wire [31:0]        trig_count,

    input  wire               opto_in,
    output wire               opto_out,
    input  wire               xvs_i,
    output wire               xvs_o,
    output wire               xvs_t,
    input  wire               xhs_i,
    output wire               xhs_o,
    output wire               xhs_t,
    output wire               xmaster_o,
    output wire               xmaster_t,
    output wire               xtrig1_o,
    output wire               xtrig1_t,
    input  wire               native_strobe_in
);
    localparam [1:0] FN_OFF = 2'd0, FN_TRIG = 2'd1, FN_SYNC = 2'd2;
    localparam [1:0] ACT_RISE = 2'd0, ACT_FALL = 2'd1, ACT_ANY = 2'd2, ACT_LEVEL = 2'd3;
    localparam [1:0] TS_OPTO = 2'd0, TS_SW = 2'd1, TS_PTP = 2'd2;
    localparam [1:0] SRC_PL = 2'd0;
    localparam [2:0] OUT_OFF=3'd0, OUT_USER=3'd1, OUT_STROBE=3'd2, OUT_EXPACT=3'd3,
                     OUT_STATUS=3'd4, OUT_SYNCOUT=3'd5, OUT_PASSTHRU=3'd6, OUT_PTPPULSE=3'd7;
    localparam [1:0] ROLE_OFF=2'd0, ROLE_MASTER=2'd1, ROLE_SLAVE=2'd2, ROLE_PTP=2'd3;
    localparam [1:0] TR_XTRIG=2'd0, TR_XVS=2'd1;
    localparam [1:0] S_IDLE=2'd0, S_DELAY=2'd1, S_ON=2'd2;

    localparam [30:0] WIDTH_MIN_NS = 31'd100;
    localparam [43:0] INC44  = {16'd0, INC_FX[47:20]} + {43'd0, INC_FX[19]};
    localparam [43:0] THR_DLY = 44'd1 - INC44;
    localparam [43:0] THR_WID = 44'd1 + INC44 + INC44;
    localparam [43:0] THR_MP  = 44'd1;
    localparam [23:0] LINE_MIN_NS  = 24'd270;

    localparam integer TICK_DIV = (CLK_HZ / 1000) * TICK_NS / 1000000;
    localparam integer PSW      = 11;
    localparam integer LOCKW    = LOCK_W;

    wire soft = soft_reset;

    reg [PSW-1:0] ps_cnt;
    reg           tick;
    always @(posedge clk) begin
        if (!rstn || !enable) begin
            ps_cnt <= {PSW{1'b0}}; tick <= 1'b0;
        end else if (ps_cnt == TICK_DIV-1) begin
            ps_cnt <= {PSW{1'b0}}; tick <= 1'b1;
        end else begin
            ps_cnt <= ps_cnt + 1'b1; tick <= 1'b0;
        end
    end

    reg [1:0] s_optoin, s_xvs, s_xhs, s_nstr;
    always @(posedge clk) begin
        s_optoin <= {s_optoin[0], opto_in};
        s_xvs    <= {s_xvs[0],    xvs_i};
        s_xhs    <= {s_xhs[0],    xhs_i};
        s_nstr   <= {s_nstr[0],   native_strobe_in};
    end
    wire optoin_s = s_optoin[1];
    wire xvs_lvl  = s_xvs[1] ^ xvs_pol;
    wire xhs_lvl  = s_xhs[1] ^ xhs_pol;
    wire nstr_s   = s_nstr[1];

    wire nstr_idle = native_low;
    wire exp_sel   = enable && (out_src == OUT_EXPACT);
    wire nstb_sel  = enable && strobe_en && (strobe_src != SRC_PL);
    reg  exp_seen, nstb_seen;
    always @(posedge clk) begin
        if (!rstn || soft || !exp_sel)  exp_seen  <= 1'b0;
        else if (nstr_s == nstr_idle)   exp_seen  <= 1'b1;
        if (!rstn || soft || !nstb_sel) nstb_seen <= 1'b0;
        else if (nstr_s == nstr_idle)   nstb_seen <= 1'b1;
    end
    wire nstr_exp = exp_seen  ? nstr_s : nstr_idle;
    wire nstr_stb = nstb_seen ? nstr_s : nstr_idle;

    reg  in_cand, in_filt;
    wire flt_restart = (optoin_s != in_cand);
    wire flt_done;
    camio_ns_timer #(.W(38)) u_tflt (.clk(clk), .clr(!rstn || !enable), .start(flt_restart),
                           .ld({2'd0, in_filter, 12'd0}), .inc(INC44[37:0]), .thr(THR_WID[37:0]), .act(), .done(flt_done));
    always @(posedge clk) begin
        if (!rstn || !enable) begin
            in_cand <= optoin_s; in_filt <= optoin_s;
        end else if (in_filter == 24'd0) begin
            in_cand <= optoin_s; in_filt <= optoin_s;
        end else begin
            if (flt_restart) in_cand <= optoin_s;
            if (flt_done && !flt_restart) in_filt <= in_cand;
        end
    end

    reg  in_prev;
    always @(posedge clk)
        if (!rstn || !enable) in_prev <= in_filt;
        else                  in_prev <= in_filt;
    wire pin_rise  = in_filt & ~in_prev;
    wire pin_fall  = ~in_filt & in_prev;
    wire trig_rise = in_invert ? pin_fall : pin_rise;
    wire trig_fall = in_invert ? pin_rise : pin_fall;

    reg trig_qual;
    always @* case (in_activation)
        ACT_RISE: trig_qual = trig_rise;
        ACT_FALL: trig_qual = trig_fall;
        ACT_ANY:  trig_qual = trig_rise | trig_fall;
        default:  trig_qual = trig_rise;
    endcase

    wire role_master = (sync_role == ROLE_MASTER);
    wire role_slave  = (sync_role == ROLE_SLAVE);
    wire role_ptp    = (sync_role == ROLE_PTP);

    wire trig_on    = enable && (in_func == FN_TRIG);
    wire wm         = (trig_src == TS_OPTO) && width_mode;
    wire route_xtrig = (sync_trig_route == TR_XTRIG);
    wire route_xvs   = (sync_trig_route == TR_XVS) && role_slave;

    reg src_start;
    always @* begin
        case (trig_src)
            TS_OPTO: src_start = wm ? trig_rise : trig_qual;
            TS_SW:   src_start = sw_trig;
            TS_PTP:  src_start = sched_fire;
            default: src_start = 1'b0;
        endcase
        src_start = src_start & trig_on;
    end
    wire src_end = trig_on & wm & trig_fall;

    reg [15:0] edge_cnt;
    always @(posedge clk)
        if (!rstn || soft) edge_cnt <= 16'd0;
        else if (src_start) edge_cnt <= edge_cnt + 1'b1;

    reg  [DIV_W-1:0] div_cnt;
    wire div_last = (in_divider <= 1) ? 1'b1 : (div_cnt == in_divider - 1'b1);
    wire trig_div = src_start & div_last;
    always @(posedge clk)
        if (!rstn || soft) div_cnt <= {DIV_W{1'b0}};
        else if (src_start) div_cnt <= div_last ? {DIV_W{1'b0}} : div_cnt + 1'b1;

    wire tclr = !rstn || !trig_on || soft;
    wire dly_act, dly_done, mp_act, xw_done;
    reg  xt_act;
    reg  armed;
    wire strobe_active;
    wire trig_busy  = dly_act | xt_act | mp_act | armed;
    wire trig_accept = trig_div & ~trig_busy;

    camio_ns_timer #(.W(44)) u_tdly (.clk(clk), .clr(tclr), .start(trig_accept),
                           .ld({1'b0, trig_delay, 12'd0}), .inc(INC44), .thr(THR_DLY), .act(dly_act), .done(dly_done));
    camio_ns_timer #(.W(44)) u_tmp  (.clk(clk), .clr(tclr), .start(trig_accept && (trig_minper != 31'd0)),
                           .ld({1'b0, trig_minper, 12'd0}), .inc(INC44), .thr(THR_MP), .act(mp_act), .done());

    reg trig_out;
    always @(posedge clk)
        if (tclr) trig_out <= 1'b0;
        else      trig_out <= dly_done;

    wire [30:0] w_eff   = (trig_width < WIDTH_MIN_NS) ? WIDTH_MIN_NS : trig_width;
    wire        xo_go   = trig_out && route_xtrig;
    wire        end_now = src_end && armed;
    camio_ns_timer #(.W(44)) u_twid (.clk(clk), .clr(tclr), .start(wm ? end_now : xo_go),
                           .ld({1'b0, (wm ? trig_delay : w_eff), 12'd0}), .inc(INC44),
                           .thr(wm ? THR_DLY : THR_WID), .act(), .done(xw_done));

    reg  rel_q;
    always @(posedge clk)
        if (tclr) rel_q <= 1'b0;
        else      rel_q <= wm && xw_done;
    wire xt_release = wm ? rel_q : xw_done;

    always @(posedge clk) begin
        if (tclr) begin
            xt_act <= 1'b0; armed <= 1'b0;
        end else begin
            if (trig_accept) armed <= wm;
            if (trig_out && !route_xtrig) armed <= 1'b0;
            if (xo_go)           xt_act <= 1'b1;
            else if (xt_release) begin xt_act <= 1'b0; armed <= 1'b0; end
        end
    end

    reg [7:0] ovr_cnt;
    reg       ovr_sticky;
    always @(posedge clk)
        if (!rstn || soft) begin ovr_cnt <= 8'd0; ovr_sticky <= 1'b0; end
        else if (trig_div & trig_busy) begin
            ovr_sticky <= 1'b1;
            if (ovr_cnt != 8'hFF) ovr_cnt <= ovr_cnt + 1'b1;
        end

    reg [31:0] trig_cnt;
    always @(posedge clk)
        if (!rstn || soft) trig_cnt <= 32'd0;
        else if (trig_out && (route_xtrig || route_xvs)) trig_cnt <= trig_cnt + 1'b1;

    wire fl_sync = enable && role_slave && (in_func == FN_SYNC);
    wire fl_ptp  = enable && role_ptp && ptp_active;
    wire fl_en   = fl_sync || (trig_on && route_xvs) || fl_ptp;
    wire fl_edge = role_ptp ? (fl_ptp && sched_fire)
                            : ((fl_sync && trig_qual) || (trig_out && route_xvs));
    reg  [23:0] lp_eff_q;
    reg         lp_zero_q;
    reg  [27:0] s12_q;
    always @(posedge clk) begin
        lp_eff_q  <= (line_period < LINE_MIN_NS) ? LINE_MIN_NS : line_period;
        lp_zero_q <= (line_period == 24'd0);
        s12_q     <= step12;
    end
    wire [23:0] n_src  = role_ptp ? ln_count  : frame_lines;
    wire [23:0] l_eff  = role_ptp ? ln_period_eff : lp_eff_q;
    wire [23:0] n_last = (n_src < 24'd2) ? 24'd1 : n_src - 1'b1;
    wire fl_cfg_bad = role_ptp ? (ln_period == 24'd0) : lp_zero_q;
    wire [37:0] fl_dec = role_ptp ? {10'd0, s12_q} : INC44[37:0];
    wire [37:0] fl_l   = {2'd0, l_eff, 12'd0};

    reg         fl_run;
    reg         fl_pend;
    reg         fl_hold;
    reg  [23:0] fl_j;
    reg  [37:0] fl_rem;
    reg         fl_frame, fl_line;
    reg         fl_early, fl_cfg_err;
    wire fl_due   = $signed(fl_rem) < 38'sd1;
    wire fl_start = (fl_edge && !fl_cfg_bad && (!fl_run || fl_due)) || (fl_pend && fl_due);
    wire fl_more  = role_ptp && ln_more;
    wire fl_lev   = fl_run && !fl_pend && (!fl_hold || fl_more) && fl_due && !fl_start;
    (* keep = "true" *) wire [37:0] fl_nx_st = fl_l - fl_dec;
    (* keep = "true" *) wire [37:0] fl_nx_ln = fl_rem + fl_l - fl_dec;
    (* keep = "true" *) wire [37:0] fl_nx_tk = fl_rem - fl_dec;
    always @(posedge clk) begin
        if (!rstn || !fl_en || soft) begin
            fl_run <= 1'b0; fl_pend <= 1'b0; fl_hold <= 1'b0; fl_j <= 24'd0; fl_rem <= 38'd0;
            fl_frame <= 1'b0; fl_line <= 1'b0;
            if (!rstn || soft) begin fl_early <= 1'b0; fl_cfg_err <= 1'b0; end
        end else begin
            fl_frame <= 1'b0; fl_line <= 1'b0;
            if (fl_edge && fl_cfg_bad) fl_cfg_err <= 1'b1;
            if (fl_edge && !fl_cfg_bad && fl_run && !(fl_hold && fl_due)) fl_early <= 1'b1;
            if (fl_start) begin
                fl_frame <= 1'b1; fl_line <= 1'b1;
                fl_run <= 1'b1; fl_pend <= 1'b0; fl_hold <= 1'b0; fl_j <= 24'd0;
                fl_rem <= fl_nx_st;
            end else begin
                if (fl_edge && !fl_cfg_bad) fl_pend <= 1'b1;
                if (fl_lev) begin
                    fl_line <= 1'b1; fl_j <= fl_j + 1'b1;
                    if (fl_j + 1'b1 >= n_last) fl_hold <= 1'b1;
                    fl_rem <= fl_nx_ln;
                end else if (fl_run && !(fl_hold && fl_due && !fl_more)) begin
                    fl_rem <= fl_nx_tk;
                end
            end
        end
    end

    localparam integer SH_XHS_DLY = 8;
    localparam integer SH_XHS_W   = 30;
    wire      sh_on   = fl_en;
    wire      f_evt   = fl_frame;
    wire      l_evt   = fl_line;
    reg       xvs_sh, xhs_sh;
    reg [5:0] xhs_cnt;
    wire [5:0] xhs_cnt_nx = l_evt ? 6'd1 :
                            (xhs_cnt == 6'd0 || xhs_cnt == SH_XHS_DLY + SH_XHS_W) ? 6'd0 : xhs_cnt + 1'b1;
    always @(posedge clk) begin
        if (!rstn || !sh_on) begin
            xvs_sh <= 1'b0; xhs_sh <= 1'b0; xhs_cnt <= 6'd0;
        end else begin
            if (f_evt)      xvs_sh <= 1'b1;
            else if (l_evt) xvs_sh <= 1'b0;
            xhs_cnt <= xhs_cnt_nx;
            xhs_sh  <= (xhs_cnt_nx > SH_XHS_DLY) && (xhs_cnt_nx <= SH_XHS_DLY + SH_XHS_W);
        end
    end

    assign xvs_t = sh_on ? 1'b0 : 1'b1;
    assign xhs_t = (sh_on && xhs_en) ? 1'b0 : 1'b1;
    assign xvs_o = xvs_sh ^ xvs_pol;
    assign xhs_o = xhs_sh ^ xhs_pol;

    wire xm_drive = enable && (role_by == 1'b0) && (sync_role != ROLE_OFF);
    assign xmaster_t = ~xm_drive;
    assign xmaster_o = ~role_master;
    assign xtrig1_t  = ~(trig_on && route_xtrig);
    assign xtrig1_o  = ~xt_act;

    reg  xvs_prev;
    always @(posedge clk) if (!rstn || !enable) xvs_prev <= 1'b0; else xvs_prev <= xvs_lvl;
    wire xvs_rise = xvs_lvl & ~xvs_prev;

    wire [DELAY_W-1:0] on_ticks  = (strobe_dur >= strobe_minon) ? strobe_dur : strobe_minon;
    wire [DELAY_W-1:0] start_off = strobe_delay;

    reg  [1:0]         st_state;
    reg  [DELAY_W-1:0] st_cnt;
    reg                strobe_pl;
    always @(posedge clk) begin
        if (!rstn || !enable || soft || !strobe_en || (strobe_src != SRC_PL)) begin
            st_state <= S_IDLE; st_cnt <= {DELAY_W{1'b0}}; strobe_pl <= 1'b0;
        end else begin
            case (st_state)
                S_IDLE: begin
                    strobe_pl <= 1'b0;
                    if (xvs_rise) begin
                        if (start_off != 0) begin st_cnt <= start_off; st_state <= S_DELAY; end
                        else begin
                            st_cnt <= on_ticks; strobe_pl <= (on_ticks != 0);
                            st_state <= (on_ticks != 0) ? S_ON : S_IDLE;
                        end
                    end
                end
                S_DELAY: if (tick) begin
                    if (st_cnt <= 1) begin
                        st_cnt <= on_ticks; strobe_pl <= (on_ticks != 0);
                        st_state <= (on_ticks != 0) ? S_ON : S_IDLE;
                    end else st_cnt <= st_cnt - 1'b1;
                end
                S_ON: if (tick) begin
                    if (st_cnt <= 1) begin strobe_pl <= 1'b0; st_state <= S_IDLE; end
                    else st_cnt <= st_cnt - 1'b1;
                end
                default: st_state <= S_IDLE;
            endcase
        end
    end
    assign strobe_active = (st_state != S_IDLE);

    wire strobe_logic  = (strobe_src == SRC_PL) ? strobe_pl : nstr_stb;
    wire strobe_out    = strobe_en ? (strobe_logic ^ strobe_invert) : 1'b0;

    localparam integer PPO_TICKS = CLK_HZ / 1000;
    localparam integer SYO_TICKS = (CLK_HZ / 1000) * (SYO_NS / 1000) / 1000;
    localparam integer PLS_MAX   = (PPO_TICKS > SYO_TICKS) ? PPO_TICKS : SYO_TICKS;
    localparam integer PLS_W     = $clog2(PLS_MAX + 1);
    wire syo_evt  = trig_on ? trig_accept : xvs_rise;
    wire pls_load = (out_src == OUT_PTPPULSE) ? evt_fire :
                    (out_src == OUT_SYNCOUT)  ? syo_evt  : 1'b0;
    wire [PLS_W-1:0] pls_len = (out_src == OUT_PTPPULSE) ? PPO_TICKS[PLS_W-1:0] - 1'b1
                                                         : SYO_TICKS[PLS_W-1:0] - 1'b1;
    reg [PLS_W-1:0] pls_cnt;
    reg             pls;
    always @(posedge clk) begin
        if (!rstn || !enable) begin
            pls_cnt <= {PLS_W{1'b0}}; pls <= 1'b0;
        end else if (pls_load) begin
            pls_cnt <= pls_len; pls <= 1'b1;
        end else if (pls_cnt != 0) begin
            pls_cnt <= pls_cnt - 1'b1;
        end else begin
            pls <= 1'b0;
        end
    end

    reg opto_pre;
    always @* begin
        case (out_src)
            OUT_USER:     opto_pre = out_user_value;
            OUT_STROBE:   opto_pre = strobe_out;
            OUT_EXPACT:   opto_pre = nstr_exp;
            OUT_STATUS:   opto_pre = ~(trig_busy | strobe_active);
            OUT_SYNCOUT:  opto_pre = pls;
            OUT_PASSTHRU: opto_pre = optoin_s;
            OUT_PTPPULSE: opto_pre = pls;
            default:      opto_pre = 1'b0;
        endcase
    end
    reg opto_out_r;
    always @(posedge clk)
        if (!rstn || !enable) opto_out_r <= 1'b0;
        else opto_out_r <= (out_src != OUT_OFF) ? (opto_pre ^ out_invert) : 1'b0;
    assign opto_out = opto_out_r;

    reg cap_lvl;
    always @* begin
        case (cap_src)
            3'd0:    cap_lvl = xvs_lvl;
            3'd1:    cap_lvl = optoin_s;
            3'd2:    cap_lvl = strobe_out;
            3'd3:    cap_lvl = xtrig1_o;
            default: cap_lvl = 1'b0;
        endcase
    end
    reg       cap_prev, cap_evt_r;
    reg [1:0] cap_warm;
    reg [3:0] cap_sel_q;
    always @(posedge clk) begin
        if (!rstn) begin cap_prev <= 1'b0; cap_evt_r <= 1'b0; cap_warm <= 2'd0; cap_sel_q <= 4'd0; end
        else begin
            cap_prev  <= cap_lvl;
            cap_sel_q <= {cap_edge, cap_src};
            if (cap_warm != 2'd3) cap_warm <= cap_warm + 1'b1;
            cap_evt_r <= (cap_warm == 2'd3) && (cap_sel_q == {cap_edge, cap_src}) &&
                         (cap_edge ? (~cap_lvl & cap_prev) : (cap_lvl & ~cap_prev));
        end
    end
    assign cap_evt = cap_evt_r;

    reg [LOCKW-1:0] lock_cnt;
    reg             locked_r;
    always @(posedge clk) begin
        if (!rstn || !enable || soft) begin lock_cnt <= 0; locked_r <= 1'b0; end
        else if (xvs_rise)            begin lock_cnt <= {LOCKW{1'b1}}; locked_r <= 1'b1; end
        else if (tick && lock_cnt != 0) lock_cnt <= lock_cnt - 1'b1;
        else if (lock_cnt == 0)       locked_r <= 1'b0;
    end

    reg optoin_prev;
    always @(posedge clk) if (!rstn || !enable) optoin_prev <= 1'b0; else optoin_prev <= optoin_s;
    wire optoin_edge = optoin_s ^ optoin_prev;
    reg [LOCKW-1:0] slv_cnt;
    reg             slave_det_r;
    always @(posedge clk) begin
        if (!rstn || !enable || soft)            begin slv_cnt <= 0; slave_det_r <= 1'b0; end
        else if (role_master && optoin_edge)     begin slv_cnt <= {LOCKW{1'b1}}; slave_det_r <= 1'b1; end
        else if (tick && slv_cnt != 0)           slv_cnt <= slv_cnt - 1'b1;
        else if (slv_cnt == 0)                   slave_det_r <= 1'b0;
    end

    wire [3:0] live_levels = {xhs_lvl, xvs_lvl, opto_out_r, optoin_s};
    wire       busy        = trig_busy | strobe_active;
    assign status = {21'd0, fl_cfg_err, fl_early, fl_run, live_levels, ovr_sticky, busy, slave_det_r, locked_r};

    assign in_status = {edge_cnt, ovr_cnt, 7'd0, in_filt};

    assign sync_status = {24'd0, 2'd0, st_state, sync_role, slave_det_r, locked_r};

    assign trig_count = trig_cnt;

endmodule

module camio_ns_timer #(
    parameter integer W = 44
)(
    input  wire         clk,
    input  wire         clr,
    input  wire         start,
    input  wire [W-1:0] ld,
    input  wire [W-1:0] inc,
    input  wire [W-1:0] thr,
    output reg          act,
    output reg          done
);
    reg [W-1:0] cnt;
    wire below = $signed(cnt) < $signed(thr);
    always @(posedge clk) begin
        if (clr) begin
            act <= 1'b0; done <= 1'b0;
        end else begin
            done <= 1'b0;
            if (start) begin
                cnt <= ld; act <= 1'b1;
            end else if (act) begin
                if (below) begin act <= 1'b0; done <= 1'b1; end
                else       cnt <= cnt - inc;
            end
        end
    end
endmodule
