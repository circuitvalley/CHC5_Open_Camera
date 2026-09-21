/*
 * pixel_packer.v - multi-format AXI4-Stream pixel packer (top)
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module pixel_packer #(
    parameter PPC = 2,
    parameter BPP = 12,
    parameter [7:0] FMT_SUPPORTED = 8'hFF
)(
    input  wire        aclk,
    input  wire        aresetn,

    input  wire [3:0]  s_axi_awaddr,
    input  wire        s_axi_awvalid,
    output wire        s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output wire        s_axi_wready,
    output wire [1:0]  s_axi_bresp,
    output wire        s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [3:0]  s_axi_araddr,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output wire        s_axi_rvalid,
    input  wire        s_axi_rready,

    input  wire [PPC*BPP-1:0] s_axis_bayer_tdata,
    input  wire               s_axis_bayer_tvalid,
    output wire               s_axis_bayer_tready,
    input  wire               s_axis_bayer_tlast,
    input  wire [0:0]         s_axis_bayer_tuser,

    input  wire [PPC*24-1:0] s_axis_rgb_tdata,
    input  wire              s_axis_rgb_tvalid,
    output wire              s_axis_rgb_tready,
    input  wire              s_axis_rgb_tlast,
    input  wire [0:0]        s_axis_rgb_tuser,

    input  wire [PPC*16-1:0] s_axis_yuv_tdata,
    input  wire              s_axis_yuv_tvalid,
    output wire              s_axis_yuv_tready,
    input  wire              s_axis_yuv_tlast,
    input  wire [0:0]        s_axis_yuv_tuser,

    output wire [PPC*16-1:0] m_axis_tdata,
    output wire              m_axis_tvalid,
    input  wire              m_axis_tready,
    output wire              m_axis_tlast,
    output wire [0:0]        m_axis_tuser,
    output wire [PPC*2-1:0]  m_axis_tkeep
);

    wire [2:0]         fmt_reg;
    wire               fmt_changed;
    wire [PPC*24-1:0]  sel_tdata;
    wire               sel_tvalid;
    wire               sel_tready;
    wire               sel_tlast;
    wire [0:0]         sel_tuser;

    localparam [2:0] PACK_THRU  = 3'd0;
    localparam [2:0] PACK_16    = 3'd1;
    localparam [2:0] PACK_RAW   = 3'd2;
    localparam [2:0] PACK_XRGB  = 3'd3;
    localparam [2:0] PACK_RGB24 = 3'd4;

    reg [2:0] fmt_q;
    reg [2:0] pack_mode;
    always @(posedge aclk) begin
        if (!aresetn) begin
            fmt_q     <= 3'd1;
            pack_mode <= PACK_RAW;
        end else begin
            fmt_q <= fmt_reg;
            case (fmt_reg)
                3'd0:    pack_mode <= PACK_16;
                3'd1:    pack_mode <= PACK_RAW;
                3'd2:    pack_mode <= PACK_THRU;
                3'd3:    pack_mode <= PACK_THRU;
                3'd4:    pack_mode <= PACK_THRU;
                3'd5:    pack_mode <= PACK_16;
                3'd6:    pack_mode <= PACK_XRGB;
                3'd7:    pack_mode <= PACK_RGB24;
                default: pack_mode <= PACK_THRU;
            endcase
        end
    end

    pixel_packer_axilite #(
        .PPC(PPC), .BPP(BPP), .FMT_SUPPORTED(FMT_SUPPORTED)
    ) u_axilite (
        .aclk(aclk), .aresetn(aresetn),
        .s_axi_awaddr(s_axi_awaddr), .s_axi_awvalid(s_axi_awvalid), .s_axi_awready(s_axi_awready),
        .s_axi_wdata(s_axi_wdata), .s_axi_wstrb(s_axi_wstrb), .s_axi_wvalid(s_axi_wvalid), .s_axi_wready(s_axi_wready),
        .s_axi_bresp(s_axi_bresp), .s_axi_bvalid(s_axi_bvalid), .s_axi_bready(s_axi_bready),
        .s_axi_araddr(s_axi_araddr), .s_axi_arvalid(s_axi_arvalid), .s_axi_arready(s_axi_arready),
        .s_axi_rdata(s_axi_rdata), .s_axi_rresp(s_axi_rresp), .s_axi_rvalid(s_axi_rvalid), .s_axi_rready(s_axi_rready),
        .fmt_reg(fmt_reg), .fmt_changed(fmt_changed)
    );

    pixel_packer_mux #(.PPC(PPC), .BPP(BPP)) u_mux (
        .fmt(fmt_q),
        .s_axis_bayer_tdata(s_axis_bayer_tdata), .s_axis_bayer_tvalid(s_axis_bayer_tvalid),
        .s_axis_bayer_tready(s_axis_bayer_tready), .s_axis_bayer_tlast(s_axis_bayer_tlast), .s_axis_bayer_tuser(s_axis_bayer_tuser),
        .s_axis_rgb_tdata(s_axis_rgb_tdata), .s_axis_rgb_tvalid(s_axis_rgb_tvalid),
        .s_axis_rgb_tready(s_axis_rgb_tready), .s_axis_rgb_tlast(s_axis_rgb_tlast), .s_axis_rgb_tuser(s_axis_rgb_tuser),
        .s_axis_yuv_tdata(s_axis_yuv_tdata), .s_axis_yuv_tvalid(s_axis_yuv_tvalid),
        .s_axis_yuv_tready(s_axis_yuv_tready), .s_axis_yuv_tlast(s_axis_yuv_tlast), .s_axis_yuv_tuser(s_axis_yuv_tuser),
        .sel_tdata(sel_tdata), .sel_tvalid(sel_tvalid), .sel_tready(sel_tready), .sel_tlast(sel_tlast), .sel_tuser(sel_tuser)
    );

    pixel_packer_core #(.PPC(PPC), .BPP(BPP)) u_core (
        .aclk(aclk), .aresetn(aresetn),
        .pack_mode(pack_mode), .fmt_changed(fmt_changed),
        .sel_tdata(sel_tdata), .sel_tvalid(sel_tvalid), .sel_tready(sel_tready), .sel_tlast(sel_tlast), .sel_tuser(sel_tuser),
        .m_axis_tdata(m_axis_tdata), .m_axis_tvalid(m_axis_tvalid), .m_axis_tready(m_axis_tready),
        .m_axis_tlast(m_axis_tlast), .m_axis_tuser(m_axis_tuser), .m_axis_tkeep(m_axis_tkeep)
    );

endmodule
