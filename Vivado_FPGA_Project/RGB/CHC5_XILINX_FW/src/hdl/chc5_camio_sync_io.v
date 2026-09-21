/*
 * chc5_camio_sync_io.v - pad-level wrapper for chc5_camio_sync (bidirectional sync pins)
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module chc5_camio_sync_io #(
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

    inout  wire        xvs,
    inout  wire        xhs,
    output wire        xmaster,
    output wire        xtrig1,
    output wire        xtrig2,
    input  wire        native_strobe_in,
    output wire        ptp_evt
);
    wire xvs_i, xvs_o, xvs_t;
    wire xhs_i, xhs_o, xhs_t;
    wire ptp_evt_o, ptp_evt_t;

    IOBUF xvs_iobuf (.I(xvs_o), .O(xvs_i), .T(xvs_t), .IO(xvs));
    IOBUF xhs_iobuf (.I(xhs_o), .O(xhs_i), .T(xhs_t), .IO(xhs));
    OBUFT ptp_evt_obuft (.I(ptp_evt_o), .T(ptp_evt_t), .O(ptp_evt));

    chc5_camio_sync #(
        .CLK_HZ(CLK_HZ), .DELAY_W(DELAY_W), .TB_W(TB_W), .DIV_W(DIV_W), .LOCK_W(LOCK_W)
    ) u_camio (
        .aclk(aclk), .aresetn(aresetn),
        .s_axi_awaddr(s_axi_awaddr), .s_axi_awprot(s_axi_awprot), .s_axi_awvalid(s_axi_awvalid), .s_axi_awready(s_axi_awready),
        .s_axi_wdata(s_axi_wdata), .s_axi_wstrb(s_axi_wstrb), .s_axi_wvalid(s_axi_wvalid), .s_axi_wready(s_axi_wready),
        .s_axi_bresp(s_axi_bresp), .s_axi_bvalid(s_axi_bvalid), .s_axi_bready(s_axi_bready),
        .s_axi_araddr(s_axi_araddr), .s_axi_arprot(s_axi_arprot), .s_axi_arvalid(s_axi_arvalid), .s_axi_arready(s_axi_arready),
        .s_axi_rdata(s_axi_rdata), .s_axi_rresp(s_axi_rresp), .s_axi_rvalid(s_axi_rvalid), .s_axi_rready(s_axi_rready),
        .opto_in(opto_in), .opto_out(opto_out),
        .xvs_i(xvs_i), .xvs_o(xvs_o), .xvs_t(xvs_t),
        .xhs_i(xhs_i), .xhs_o(xhs_o), .xhs_t(xhs_t),
        .xmaster(xmaster), .xtrig1(xtrig1), .xtrig2(xtrig2),
        .native_strobe_in(native_strobe_in),
        .ptp_evt_o(ptp_evt_o), .ptp_evt_t(ptp_evt_t));

endmodule
