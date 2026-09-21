/*
 * chc5_camio_sync.v - camera aux I/O and multi-camera sync (top)
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module chc5_camio_sync #(
    parameter integer CLK_HZ  = 150_000_000,
    parameter integer DELAY_W = 16,
    parameter integer TB_W    = 20,
    parameter integer DIV_W   = 8,
    parameter integer LOCK_W  = 14
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI, ASSOCIATED_RESET aresetn" *)
    input  wire        aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire        aresetn,

    (* X_INTERFACE_PARAMETER = "PROTOCOL AXI4LITE, ADDR_WIDTH 9, DATA_WIDTH 32" *)
    input  wire [8:0]  s_axi_awaddr,
    input  wire [2:0]  s_axi_awprot,
    input  wire        s_axi_awvalid,
    output wire        s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output wire        s_axi_wready,
    output wire [1:0]  s_axi_bresp,
    output wire        s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [8:0]  s_axi_araddr,
    input  wire [2:0]  s_axi_arprot,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output wire        s_axi_rvalid,
    input  wire        s_axi_rready,

    input  wire        opto_in,
    output wire        opto_out,

    input  wire        xvs_i,
    output wire        xvs_o,
    output wire        xvs_t,
    input  wire        xhs_i,
    output wire        xhs_o,
    output wire        xhs_t,
    output wire        xmaster,
    output wire        xtrig1,
    output wire        xtrig2,
    input  wire        native_strobe_in,

    output wire        ptp_evt_o,
    output wire        ptp_evt_t
);
    localparam [63:0] INC_FX64 = ((64'd1_000_000_000 << 32) + (CLK_HZ / 2)) / CLK_HZ;
    localparam [47:0] INC_FX   = INC_FX64[47:0];

    wire               en, soft_rst;
    wire [1:0]         in_func, in_act;
    wire               in_inv;
    wire [DELAY_W-1:0] in_delay;
    wire [DIV_W-1:0]   in_div;
    wire               st_en, st_inv;
    wire [1:0]         st_src;
    wire [DELAY_W-1:0] st_delay, st_dur, st_minon;
    wire [2:0]         out_src;
    wire               out_inv, out_uval;
    wire [1:0]         sy_role, sy_troute;
    wire               sy_xvsdir, sy_xhsdir, sy_xvspol, sy_xhspol, sy_roleby;

    wire [31:0]        status_w, in_status_w, sync_status_w;

    wire               xhs_en, cap_edge;
    wire [2:0]         cap_src;
    wire               tb_en, evt_en, sched_en, load_pulse, commit_pulse, commit_busy, evt_arm, evt_pol;
    wire               cap_en, cap_latch, cap_pop;
    wire [63:0]        load_val, evt_next, sched_next, sched_period, time_now;
    wire [31:0]        rate_adj, evt_period, sched_period_frac;
    wire [23:0]        line_period, frame_lines;
    wire [7:0]         evt_width;
    wire [31:0]        ptp_status_w, evt_count_w, cap_head_lo_w, cap_hold_hi_w, cap_hold_meta_w, cap_status_w;
    wire               sched_fire, line_tick, ptp_active, cap_evt;
    wire               evt_fire;

    camio_sync_axil_regs #(.DELAY_W(DELAY_W), .TB_W(TB_W), .DIV_W(DIV_W), .INC_FRAC(INC_FX[31:0])) u_regs (
        .aclk(aclk), .aresetn(aresetn),
        .s_axi_awaddr(s_axi_awaddr), .s_axi_awprot(s_axi_awprot), .s_axi_awvalid(s_axi_awvalid), .s_axi_awready(s_axi_awready),
        .s_axi_wdata(s_axi_wdata), .s_axi_wstrb(s_axi_wstrb), .s_axi_wvalid(s_axi_wvalid), .s_axi_wready(s_axi_wready),
        .s_axi_bresp(s_axi_bresp), .s_axi_bvalid(s_axi_bvalid), .s_axi_bready(s_axi_bready),
        .s_axi_araddr(s_axi_araddr), .s_axi_arprot(s_axi_arprot), .s_axi_arvalid(s_axi_arvalid), .s_axi_arready(s_axi_arready),
        .s_axi_rdata(s_axi_rdata), .s_axi_rresp(s_axi_rresp), .s_axi_rvalid(s_axi_rvalid), .s_axi_rready(s_axi_rready),
        .o_enable(en), .o_soft_reset(soft_rst),
        .o_in_func(in_func), .o_in_activation(in_act), .o_in_invert(in_inv),
        .o_in_delay(in_delay), .o_in_divider(in_div),
        .o_strobe_en(st_en), .o_strobe_invert(st_inv), .o_strobe_src(st_src),
        .o_strobe_delay(st_delay),
        .o_strobe_dur(st_dur), .o_strobe_minon(st_minon),
        .o_out_src(out_src), .o_out_invert(out_inv), .o_out_user_value(out_uval),
        .o_sync_role(sy_role), .o_xvs_dir(sy_xvsdir), .o_xhs_dir(sy_xhsdir),
        .o_xvs_pol(sy_xvspol), .o_xhs_pol(sy_xhspol), .o_role_by(sy_roleby),
        .o_sync_trig_route(sy_troute),
        .o_xhs_en(xhs_en), .o_cap_src(cap_src), .o_cap_edge(cap_edge),
        .o_tb_en(tb_en), .o_evt_en(evt_en), .o_sched_en(sched_en),
        .o_load_pulse(load_pulse), .o_load_val(load_val), .o_commit_pulse(commit_pulse),
        .o_rate_adj(rate_adj), .o_evt_arm(evt_arm), .o_evt_next(evt_next),
        .o_evt_period(evt_period), .o_evt_width(evt_width), .o_evt_pol(evt_pol),
        .o_sched_next(sched_next), .o_sched_period(sched_period), .o_sched_period_frac(sched_period_frac),
        .o_line_period(line_period), .o_frame_lines(frame_lines),
        .o_cap_en(cap_en), .o_cap_latch(cap_latch), .o_cap_pop(cap_pop),
        .i_status(status_w), .i_in_status(in_status_w), .i_sync_status(sync_status_w),
        .i_time(time_now), .i_ptp_status(ptp_status_w), .i_evt_count(evt_count_w),
        .i_cap_head_lo(cap_head_lo_w), .i_cap_hold_hi(cap_hold_hi_w),
        .i_cap_hold_meta(cap_hold_meta_w), .i_cap_status(cap_status_w), .i_commit_busy(commit_busy));

    camio_sync_core #(.CLK_HZ(CLK_HZ), .DELAY_W(DELAY_W), .TB_W(TB_W), .DIV_W(DIV_W), .LOCK_W(LOCK_W)) u_core (
        .clk(aclk), .rstn(aresetn),
        .enable(en), .soft_reset(soft_rst),
        .in_func(in_func), .in_activation(in_act), .in_invert(in_inv),
        .in_delay(in_delay), .in_divider(in_div),
        .strobe_en(st_en), .strobe_invert(st_inv), .strobe_src(st_src),
        .strobe_delay(st_delay),
        .strobe_dur(st_dur), .strobe_minon(st_minon),
        .out_src(out_src), .out_invert(out_inv), .out_user_value(out_uval),
        .sync_role(sy_role), .xvs_dir(sy_xvsdir), .xhs_dir(sy_xhsdir),
        .xvs_pol(sy_xvspol), .xhs_pol(sy_xhspol), .role_by(sy_roleby),
        .sync_trig_route(sy_troute),
        .xhs_en(xhs_en), .cap_src(cap_src), .cap_edge(cap_edge),
        .sched_fire(sched_fire), .line_tick(line_tick), .ptp_active(ptp_active), .cap_evt(cap_evt),
        .evt_fire(evt_fire),
        .status(status_w), .in_status(in_status_w), .sync_status(sync_status_w),
        .opto_in(opto_in), .opto_out(opto_out),
        .xvs_i(xvs_i), .xvs_o(xvs_o), .xvs_t(xvs_t),
        .xhs_i(xhs_i), .xhs_o(xhs_o), .xhs_t(xhs_t),
        .xmaster(xmaster), .xtrig1(xtrig1), .xtrig2(xtrig2),
        .native_strobe_in(native_strobe_in));

    chc5_ptp_timebase #(.INC_FX(INC_FX)) u_ptp (
        .clk(aclk), .rstn(aresetn),
        .tb_en(tb_en), .evt_en(evt_en), .sched_en(sched_en),
        .load_pulse(load_pulse), .load_val(load_val), .commit_pulse(commit_pulse),
        .rate_adj(rate_adj), .evt_arm(evt_arm), .evt_next_val(evt_next),
        .evt_period(evt_period), .evt_width(evt_width), .evt_pol(evt_pol),
        .sched_next(sched_next), .sched_period(sched_period), .sched_period_frac(sched_period_frac),
        .line_period(line_period), .frame_lines(frame_lines), .commit_busy(commit_busy),
        .cap_en(cap_en), .cap_tag({cap_edge, cap_src}), .cap_latch(cap_latch), .cap_pop(cap_pop),
        .sched_fire(sched_fire), .line_tick(line_tick), .ptp_active(ptp_active), .cap_evt(cap_evt),
        .evt_fire(evt_fire),
        .ptp_evt_o(ptp_evt_o), .ptp_evt_t(ptp_evt_t),
        .time_now(time_now), .ptp_status(ptp_status_w), .evt_count(evt_count_w),
        .cap_head_lo(cap_head_lo_w), .cap_hold_hi(cap_hold_hi_w),
        .cap_hold_meta(cap_hold_meta_w), .cap_status(cap_status_w));

endmodule
