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
    parameter integer LOCK_W   = 14
)(
    input  wire               clk,
    input  wire               rstn,

    input  wire               enable,
    input  wire               soft_reset,
    input  wire [1:0]         in_func,
    input  wire [1:0]         in_activation,
    input  wire               in_invert,
    input  wire [DELAY_W-1:0] in_delay,
    input  wire [DIV_W-1:0]   in_divider,
    input  wire               strobe_en,
    input  wire               strobe_invert,
    input  wire [1:0]         strobe_src,
    input  wire [DELAY_W-1:0] strobe_delay,
    input  wire [DELAY_W-1:0] strobe_dur,
    input  wire [DELAY_W-1:0] strobe_minon,
    input  wire [2:0]         out_src,
    input  wire               out_invert,
    input  wire               out_user_value,
    input  wire [1:0]         sync_role,
    input  wire               xvs_dir,
    input  wire               xhs_dir,
    input  wire               xvs_pol,
    input  wire               xhs_pol,
    input  wire               role_by,
    input  wire [1:0]         sync_trig_route,
    input  wire               xhs_en,
    input  wire [2:0]         cap_src,
    input  wire               cap_edge,

    input  wire               sched_fire,
    input  wire               line_tick,
    input  wire               ptp_active,
    input  wire               evt_fire,
    output wire               cap_evt,

    output wire [31:0]        status,
    output wire [31:0]        in_status,
    output wire [31:0]        sync_status,

    input  wire               opto_in,
    output wire               opto_out,
    input  wire               xvs_i,
    output wire               xvs_o,
    output wire               xvs_t,
    input  wire               xhs_i,
    output wire               xhs_o,
    output wire               xhs_t,
    output wire               xmaster,
    output wire               xtrig1,
    output wire               xtrig2,
    input  wire               native_strobe_in
);
    localparam [1:0] FN_OFF = 2'd0, FN_TRIG = 2'd1, FN_SYNC = 2'd2;
    localparam [1:0] ACT_RISE = 2'd0, ACT_FALL = 2'd1, ACT_ANY = 2'd2, ACT_LEVEL = 2'd3;
    localparam [1:0] SRC_PL = 2'd0;
    localparam [2:0] OUT_OFF=3'd0, OUT_USER=3'd1, OUT_STROBE=3'd2, OUT_EXPACT=3'd3,
                     OUT_STATUS=3'd4, OUT_SYNCOUT=3'd5, OUT_PASSTHRU=3'd6, OUT_PTPPULSE=3'd7;
    localparam [1:0] ROLE_OFF=2'd0, ROLE_MASTER=2'd1, ROLE_SLAVE=2'd2, ROLE_PTP=2'd3;
    localparam [1:0] TR_XTRIG=2'd0, TR_XVS=2'd1, TR_I2C=2'd2;
    localparam [1:0] S_IDLE=2'd0, S_DELAY=2'd1, S_ON=2'd2;

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

    reg [1:0] db_sh;
    reg       optoin_db;
    always @(posedge clk) begin
        if (!rstn || !enable) begin
            db_sh <= 2'b00; optoin_db <= 1'b0;
        end else if (tick) begin
            db_sh <= {db_sh[0], optoin_s};
            if      ( optoin_s & db_sh[0] & db_sh[1]) optoin_db <= 1'b1;
            else if (~optoin_s & ~db_sh[0] & ~db_sh[1]) optoin_db <= 1'b0;
        end
    end

    wire trig_level = optoin_db ^ in_invert;
    reg  trig_prev;
    always @(posedge clk) if (!rstn || !enable) trig_prev <= 1'b0; else trig_prev <= trig_level;
    wire trig_rise = trig_level & ~trig_prev;
    wire trig_fall = ~trig_level & trig_prev;

    reg trig_qual;
    always @* case (in_activation)
        ACT_RISE: trig_qual = trig_rise;
        ACT_FALL: trig_qual = trig_fall;
        ACT_ANY:  trig_qual = trig_rise | trig_fall;
        default:  trig_qual = trig_rise;
    endcase
    wire trig_func = (in_func == FN_TRIG);
    wire trig_evt  = trig_func & trig_qual;

    reg [15:0] edge_cnt;
    always @(posedge clk)
        if (!rstn || soft) edge_cnt <= 16'd0;
        else if (trig_evt) edge_cnt <= edge_cnt + 1'b1;

    reg  [DIV_W-1:0] div_cnt;
    wire div_last = (in_divider <= 1) ? 1'b1 : (div_cnt == in_divider - 1'b1);
    wire trig_div = trig_evt & div_last;
    always @(posedge clk)
        if (!rstn || soft) div_cnt <= {DIV_W{1'b0}};
        else if (trig_evt) div_cnt <= div_last ? {DIV_W{1'b0}} : div_cnt + 1'b1;

    wire strobe_active;
    reg  tdly_run;
    wire busy = tdly_run | strobe_active;

    wire trig_accept = trig_div & ~busy;

    reg  [DELAY_W-1:0] tdly_cnt;
    reg                trig_out;
    always @(posedge clk) begin
        if (!rstn || !enable || soft) begin
            tdly_run <= 1'b0; tdly_cnt <= {DELAY_W{1'b0}}; trig_out <= 1'b0;
        end else begin
            trig_out <= 1'b0;
            if (trig_accept) begin
                if (in_delay == 0) trig_out <= 1'b1;
                else begin tdly_run <= 1'b1; tdly_cnt <= in_delay; end
            end else if (tdly_run && tick) begin
                if (tdly_cnt <= 1) begin tdly_run <= 1'b0; trig_out <= 1'b1; end
                else tdly_cnt <= tdly_cnt - 1'b1;
            end
        end
    end

    reg [7:0] ovr_cnt;
    reg       ovr_sticky;
    always @(posedge clk)
        if (!rstn || soft) begin ovr_cnt <= 8'd0; ovr_sticky <= 1'b0; end
        else if (trig_div & busy) begin
            ovr_sticky <= 1'b1;
            if (ovr_cnt != 8'hFF) ovr_cnt <= ovr_cnt + 1'b1;
        end

    reg trigo_wide, trigo_arm;
    always @(posedge clk) begin
        if (!rstn || !enable || soft) begin trigo_wide <= 1'b0; trigo_arm <= 1'b0; end
        else if (trig_out)            begin trigo_wide <= 1'b1; trigo_arm <= 1'b1; end
        else if (tick && trigo_arm)   begin trigo_wide <= 1'b0; trigo_arm <= 1'b0; end
    end

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

    wire strobe_logic  = (strobe_src == SRC_PL) ? strobe_pl : nstr_s;
    wire strobe_out    = strobe_en ? (strobe_logic ^ strobe_invert) : 1'b0;

    wire role_master = (sync_role == ROLE_MASTER);

    assign xmaster = (enable && role_by == 1'b0) ? ~role_master : 1'b1;

    localparam integer PTP_XHS_DLY = 8;
    localparam integer PTP_XHS_W   = 30;
    wire      role_ptp = (sync_role == ROLE_PTP);
    reg       xvs_ptp, xhs_ptp;
    reg [5:0] xhs_cnt;
    wire [5:0] xhs_cnt_nx = line_tick ? 6'd1 :
                            (xhs_cnt == 6'd0 || xhs_cnt == PTP_XHS_DLY + PTP_XHS_W) ? 6'd0 : xhs_cnt + 1'b1;
    always @(posedge clk) begin
        if (!rstn || !enable || !role_ptp || !ptp_active) begin
            xvs_ptp <= 1'b0; xhs_ptp <= 1'b0; xhs_cnt <= 6'd0;
        end else begin
            if (sched_fire)     xvs_ptp <= 1'b1;
            else if (line_tick) xvs_ptp <= 1'b0;
            xhs_cnt <= xhs_cnt_nx;
            xhs_ptp <= (xhs_cnt_nx > PTP_XHS_DLY) && (xhs_cnt_nx <= PTP_XHS_DLY + PTP_XHS_W);
        end
    end

    wire xvs_has_source = role_ptp ? ptp_active
                                   : ((sync_trig_route == TR_XVS && trig_func) || (in_func == FN_SYNC));
    wire xhs_has_source = role_ptp & ptp_active & xhs_en;
    assign xvs_t = (enable && xvs_has_source) ? 1'b0 : 1'b1;
    assign xhs_t = (enable && xhs_has_source) ? 1'b0 : 1'b1;

    wire xvs_drive = role_ptp                                 ? xvs_ptp    :
                     (sync_trig_route == TR_XVS && trig_func) ? trigo_wide :
                     (in_func == FN_SYNC)                     ? optoin_s   : 1'b0;
    assign xvs_o = xvs_drive ^ xvs_pol;
    assign xhs_o = role_ptp ? (xhs_ptp ^ xhs_pol) : 1'b0;

    assign xtrig1 = (enable && sync_trig_route == TR_XTRIG && trig_func) ? trigo_wide : 1'b0;
    assign xtrig2 = 1'b0;

    localparam integer PPO_TICKS = CLK_HZ / 1000;
    localparam integer PPO_W     = $clog2(PPO_TICKS + 1);
    reg [PPO_W-1:0] ppo_cnt;
    reg             ppo;
    always @(posedge clk) begin
        if (!rstn || !enable) begin
            ppo_cnt <= {PPO_W{1'b0}}; ppo <= 1'b0;
        end else if (evt_fire) begin
            ppo_cnt <= PPO_TICKS[PPO_W-1:0] - 1'b1; ppo <= 1'b1;
        end else if (ppo_cnt != 0) begin
            ppo_cnt <= ppo_cnt - 1'b1;
        end else begin
            ppo <= 1'b0;
        end
    end

    reg opto_pre;
    always @* begin
        case (out_src)
            OUT_USER:     opto_pre = out_user_value;
            OUT_STROBE:   opto_pre = strobe_out;
            OUT_EXPACT:   opto_pre = nstr_s;
            OUT_STATUS:   opto_pre = ~busy;
            OUT_SYNCOUT:  opto_pre = xvs_lvl;
            OUT_PASSTHRU: opto_pre = optoin_s;
            OUT_PTPPULSE: opto_pre = ppo;
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
            3'd3:    cap_lvl = xtrig1;
            default: cap_lvl = 1'b0;
        endcase
    end
    reg       cap_prev, cap_evt_r;
    reg [1:0] cap_warm;
    always @(posedge clk) begin
        if (!rstn) begin cap_prev <= 1'b0; cap_evt_r <= 1'b0; cap_warm <= 2'd0; end
        else begin
            cap_prev  <= cap_lvl;
            if (cap_warm != 2'd3) cap_warm <= cap_warm + 1'b1;
            cap_evt_r <= (cap_warm == 2'd3) &&
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
    assign status = {24'd0, live_levels, ovr_sticky, busy, slave_det_r, locked_r};

    assign in_status = {edge_cnt, ovr_cnt, 7'd0, optoin_db};

    assign sync_status = {24'd0, 2'd0, st_state, sync_role, slave_det_r, locked_r};

endmodule
