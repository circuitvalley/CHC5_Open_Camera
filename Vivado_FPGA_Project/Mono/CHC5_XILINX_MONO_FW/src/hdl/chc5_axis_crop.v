/*
 * chc5_axis_crop.v - programmable crop window for RAW/Bayer AXI4-Stream video (top)
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module chc5_axis_crop #(
    parameter integer PPC             = 4,
    parameter integer COMPONENT_WIDTH = 12,
    parameter integer MAX_COLS        = 8192,
    parameter integer MAX_ROWS        = 8192
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXIS:M_AXIS:S_AXI, ASSOCIATED_RESET aresetn" *)
    input  wire                    aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire                    aresetn,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TDATA" *)
    (* X_INTERFACE_PARAMETER = "HAS_TKEEP 1, HAS_TSTRB 0, HAS_TREADY 1, HAS_TLAST 1, TUSER_WIDTH 1" *)
    input  wire [PPC*COMPONENT_WIDTH-1:0]        s_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TKEEP" *)
    input  wire [(PPC*COMPONENT_WIDTH+7)/8-1:0]  s_axis_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TVALID" *)
    input  wire                    s_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TREADY" *)
    output wire                    s_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TLAST" *)
    input  wire                    s_axis_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TUSER" *)
    input  wire [0:0]              s_axis_tuser,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TDATA" *)
    (* X_INTERFACE_PARAMETER = "HAS_TKEEP 1, HAS_TSTRB 0, HAS_TREADY 1, HAS_TLAST 1, TUSER_WIDTH 1" *)
    output wire [PPC*COMPONENT_WIDTH-1:0]        m_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TKEEP" *)
    output wire [(PPC*COMPONENT_WIDTH+7)/8-1:0]  m_axis_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TVALID" *)
    output wire                    m_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TREADY" *)
    input  wire                    m_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TLAST" *)
    output wire                    m_axis_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TUSER" *)
    output wire [0:0]              m_axis_tuser,

    (* X_INTERFACE_PARAMETER = "PROTOCOL AXI4LITE, ADDR_WIDTH 12, DATA_WIDTH 32" *)
    input  wire [11:0]             s_axi_awaddr,
    input  wire [2:0]              s_axi_awprot,
    input  wire                    s_axi_awvalid,
    output wire                    s_axi_awready,
    input  wire [31:0]             s_axi_wdata,
    input  wire [3:0]              s_axi_wstrb,
    input  wire                    s_axi_wvalid,
    output wire                    s_axi_wready,
    output wire [1:0]              s_axi_bresp,
    output wire                    s_axi_bvalid,
    input  wire                    s_axi_bready,
    input  wire [11:0]             s_axi_araddr,
    input  wire [2:0]              s_axi_arprot,
    input  wire                    s_axi_arvalid,
    output wire                    s_axi_arready,
    output wire [31:0]             s_axi_rdata,
    output wire [1:0]              s_axi_rresp,
    output wire                    s_axi_rvalid,
    input  wire                    s_axi_rready
);

    localparam integer TDW   = PPC * COMPONENT_WIDTH;
    localparam integer TKW   = (TDW + 7) / 8;
    localparam integer DIM_W = 16;
    localparam integer SEL_W = (PPC <= 1) ? 1 : (PPC <= 2) ? 2 : (PPC <= 4) ? 3 : 4;

    wire                 cfg_enable, cfg_bypass, cfg_drop_until_sof;
    wire [DIM_W-1:0]     cfg_x, cfg_y, cfg_w, cfg_h;
    wire [DIM_W-1:0]     cfg_y_end, cfg_start_beat, cfg_nbeats_m1;
    wire [SEL_W-1:0]     cfg_sel;

    wire [DIM_W-1:0]     act_x, act_y, act_w, act_h;
    wire [DIM_W-1:0]     geom_w, geom_h;
    wire [31:0]          frame_cnt;
    wire                 sts_short_line, sts_window_oob, sts_sof_resync;

    crop_axil_regs #(
        .PPC(PPC), .COMPONENT_WIDTH(COMPONENT_WIDTH),
        .MAX_COLS(MAX_COLS), .MAX_ROWS(MAX_ROWS), .DIM_W(DIM_W)
    ) u_regs (
        .aclk(aclk), .aresetn(aresetn),
        .s_axi_awaddr(s_axi_awaddr), .s_axi_awprot(s_axi_awprot),
        .s_axi_awvalid(s_axi_awvalid), .s_axi_awready(s_axi_awready),
        .s_axi_wdata(s_axi_wdata), .s_axi_wstrb(s_axi_wstrb),
        .s_axi_wvalid(s_axi_wvalid), .s_axi_wready(s_axi_wready),
        .s_axi_bresp(s_axi_bresp), .s_axi_bvalid(s_axi_bvalid),
        .s_axi_bready(s_axi_bready),
        .s_axi_araddr(s_axi_araddr), .s_axi_arprot(s_axi_arprot),
        .s_axi_arvalid(s_axi_arvalid), .s_axi_arready(s_axi_arready),
        .s_axi_rdata(s_axi_rdata), .s_axi_rresp(s_axi_rresp),
        .s_axi_rvalid(s_axi_rvalid), .s_axi_rready(s_axi_rready),
        .cfg_enable(cfg_enable), .cfg_bypass(cfg_bypass),
        .cfg_drop_until_sof(cfg_drop_until_sof),
        .cfg_x(cfg_x), .cfg_y(cfg_y), .cfg_w(cfg_w), .cfg_h(cfg_h),
        .cfg_y_end(cfg_y_end), .cfg_start_beat(cfg_start_beat),
        .cfg_nbeats_m1(cfg_nbeats_m1), .cfg_sel(cfg_sel),
        .act_x(act_x), .act_y(act_y), .act_w(act_w), .act_h(act_h),
        .geom_w(geom_w), .geom_h(geom_h), .frame_cnt(frame_cnt),
        .sts_short_line(sts_short_line), .sts_window_oob(sts_window_oob),
        .sts_sof_resync(sts_sof_resync));

    wire [TDW-1:0] ci_tdata, co_tdata;
    wire [TKW-1:0] ci_tkeep, co_tkeep;
    wire           ci_tvalid, ci_tready, ci_tlast;
    wire           co_tvalid, co_tready, co_tlast;
    wire [0:0]     ci_tuser,  co_tuser;

    chc5_axis_skid #(.DW(TDW), .KW(TKW)) u_skid_in (
        .aclk(aclk), .aresetn(aresetn),
        .s_tdata(s_axis_tdata), .s_tkeep(s_axis_tkeep), .s_tvalid(s_axis_tvalid),
        .s_tready(s_axis_tready), .s_tlast(s_axis_tlast), .s_tuser(s_axis_tuser),
        .m_tdata(ci_tdata), .m_tkeep(ci_tkeep), .m_tvalid(ci_tvalid),
        .m_tready(ci_tready), .m_tlast(ci_tlast), .m_tuser(ci_tuser));

    crop_core #(
        .PPC(PPC), .COMPONENT_WIDTH(COMPONENT_WIDTH), .DIM_W(DIM_W)
    ) u_core (
        .aclk(aclk), .aresetn(aresetn),
        .s_tdata(ci_tdata), .s_tkeep(ci_tkeep), .s_tvalid(ci_tvalid),
        .s_tready(ci_tready), .s_tlast(ci_tlast), .s_tuser(ci_tuser),
        .m_tdata(co_tdata), .m_tkeep(co_tkeep), .m_tvalid(co_tvalid),
        .m_tready(co_tready), .m_tlast(co_tlast), .m_tuser(co_tuser),
        .cfg_enable(cfg_enable), .cfg_bypass(cfg_bypass),
        .cfg_drop_until_sof(cfg_drop_until_sof),
        .cfg_x(cfg_x), .cfg_y(cfg_y), .cfg_w(cfg_w), .cfg_h(cfg_h),
        .cfg_y_end(cfg_y_end), .cfg_start_beat(cfg_start_beat),
        .cfg_nbeats_m1(cfg_nbeats_m1), .cfg_sel(cfg_sel),
        .act_x(act_x), .act_y(act_y), .act_w(act_w), .act_h(act_h),
        .geom_w(geom_w), .geom_h(geom_h), .frame_cnt(frame_cnt),
        .sts_short_line(sts_short_line), .sts_window_oob(sts_window_oob),
        .sts_sof_resync(sts_sof_resync));

    chc5_axis_skid #(.DW(TDW), .KW(TKW)) u_skid_out (
        .aclk(aclk), .aresetn(aresetn),
        .s_tdata(co_tdata), .s_tkeep(co_tkeep), .s_tvalid(co_tvalid),
        .s_tready(co_tready), .s_tlast(co_tlast), .s_tuser(co_tuser),
        .m_tdata(m_axis_tdata), .m_tkeep(m_axis_tkeep), .m_tvalid(m_axis_tvalid),
        .m_tready(m_axis_tready), .m_tlast(m_axis_tlast), .m_tuser(m_axis_tuser));

endmodule
