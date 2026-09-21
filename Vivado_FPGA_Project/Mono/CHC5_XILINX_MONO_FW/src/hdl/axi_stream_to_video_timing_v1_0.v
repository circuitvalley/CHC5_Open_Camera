/*
 * axi_stream_to_video_timing_v1_0.v - AXI4-Lite wrapper for axi_stream_to_video_timing
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1ns / 1ps
module axi_stream_to_video_timing_v1_0
#(
    parameter integer C_LINES_PER_FRAME_INIT = 2160
)
(
    input  wire                                s_axi_aclk,
    input  wire                                s_axi_aresetn,
    input  wire [3:0]                          s_axi_awaddr,
    input  wire [2:0]                          s_axi_awprot,
    input  wire                                s_axi_awvalid,
    output wire                                s_axi_awready,
    input  wire [31:0]                         s_axi_wdata,
    input  wire [3:0]                          s_axi_wstrb,
    input  wire                                s_axi_wvalid,
    output wire                                s_axi_wready,
    output wire [1:0]                          s_axi_bresp,
    output wire                                s_axi_bvalid,
    input  wire                                s_axi_bready,
    input  wire [3:0]                          s_axi_araddr,
    input  wire [2:0]                          s_axi_arprot,
    input  wire                                s_axi_arvalid,
    output wire                                s_axi_arready,
    output wire [31:0]                         s_axi_rdata,
    output wire [1:0]                          s_axi_rresp,
    output wire                                s_axi_rvalid,
    input  wire                                s_axi_rready,

    input  wire                                clk_in,
    input  wire                                reset_n,
    input  wire                                s_axis_video_tlast,
    input  wire                                s_axis_video_tuser,
    input  wire                                s_axis_video_tvalid,
    output wire                                s_axis_video_tready,
    output wire                                vt_fsync,
    output wire                                vt_lsync
);

reg  axi_awready;
reg  axi_wready;
reg  axi_bvalid;
reg  axi_arready;
reg  axi_rvalid;
reg  [31:0] axi_rdata;

localparam [31:0] VERSION_VAL = 32'h5654_0100;
localparam [31:0] CONFIG_VAL  = {19'd0, C_LINES_PER_FRAME_INIT[12:0]};

localparam [1:0] A_LINES   = 2'd0;
localparam [1:0] A_VERSION = 2'd1;
localparam [1:0] A_CONFIG  = 2'd2;
localparam [1:0] A_SCRATCH = 2'd3;

reg  [12:0] slv_lines_per_frame;
reg  [31:0] slv_scratch;

assign s_axi_awready = axi_awready;
assign s_axi_wready  = axi_wready;
assign s_axi_bresp   = 2'b00;
assign s_axi_bvalid  = axi_bvalid;
assign s_axi_arready = axi_arready;
assign s_axi_rdata   = axi_rdata;
assign s_axi_rresp   = 2'b00;
assign s_axi_rvalid  = axi_rvalid;

always @(posedge s_axi_aclk)
begin
    if (!s_axi_aresetn)
        axi_awready <= 1'b0;
    else if (~axi_awready && s_axi_awvalid && s_axi_wvalid)
        axi_awready <= 1'b1;
    else
        axi_awready <= 1'b0;
end

always @(posedge s_axi_aclk)
begin
    if (!s_axi_aresetn)
        axi_wready <= 1'b0;
    else if (~axi_wready && s_axi_awvalid && s_axi_wvalid)
        axi_wready <= 1'b1;
    else
        axi_wready <= 1'b0;
end

always @(posedge s_axi_aclk)
begin
    if (!s_axi_aresetn)
    begin
        slv_lines_per_frame <= C_LINES_PER_FRAME_INIT[12:0];
        slv_scratch         <= 32'd0;
    end
    else if (axi_awready && s_axi_awvalid && axi_wready && s_axi_wvalid)
    begin
        case (s_axi_awaddr[3:2])
        A_LINES: begin
            if (s_axi_wstrb[0])
                slv_lines_per_frame[7:0]  <= s_axi_wdata[7:0];
            if (s_axi_wstrb[1])
                slv_lines_per_frame[12:8] <= s_axi_wdata[12:8];
        end
        A_SCRATCH: begin
            if (s_axi_wstrb[0]) slv_scratch[7:0]   <= s_axi_wdata[7:0];
            if (s_axi_wstrb[1]) slv_scratch[15:8]  <= s_axi_wdata[15:8];
            if (s_axi_wstrb[2]) slv_scratch[23:16] <= s_axi_wdata[23:16];
            if (s_axi_wstrb[3]) slv_scratch[31:24] <= s_axi_wdata[31:24];
        end
        default: ;
        endcase
    end
end

always @(posedge s_axi_aclk)
begin
    if (!s_axi_aresetn)
        axi_bvalid <= 1'b0;
    else if (axi_awready && s_axi_awvalid && axi_wready && s_axi_wvalid && ~axi_bvalid)
        axi_bvalid <= 1'b1;
    else if (s_axi_bready && axi_bvalid)
        axi_bvalid <= 1'b0;
end

always @(posedge s_axi_aclk)
begin
    if (!s_axi_aresetn)
        axi_arready <= 1'b0;
    else if (~axi_arready && s_axi_arvalid && ~axi_rvalid)
        axi_arready <= 1'b1;
    else
        axi_arready <= 1'b0;
end

always @(posedge s_axi_aclk)
begin
    if (!s_axi_aresetn)
    begin
        axi_rvalid <= 1'b0;
        axi_rdata  <= 0;
    end
    else if (axi_arready && s_axi_arvalid && ~axi_rvalid)
    begin
        axi_rvalid <= 1'b1;
        case (s_axi_araddr[3:2])
        A_LINES:   axi_rdata <= {19'd0, slv_lines_per_frame};
        A_VERSION: axi_rdata <= VERSION_VAL;
        A_CONFIG:  axi_rdata <= CONFIG_VAL;
        A_SCRATCH: axi_rdata <= slv_scratch;
        endcase
    end
    else if (axi_rvalid && s_axi_rready)
    begin
        axi_rvalid <= 1'b0;
    end
end

(* ASYNC_REG = "TRUE" *) reg [12:0] sync_lines_meta;
(* ASYNC_REG = "TRUE" *) reg [12:0] lines_per_frame_sync;

always @(posedge clk_in)
begin
    if (!reset_n)
    begin
        sync_lines_meta      <= C_LINES_PER_FRAME_INIT[12:0];
        lines_per_frame_sync <= C_LINES_PER_FRAME_INIT[12:0];
    end
    else
    begin
        sync_lines_meta      <= slv_lines_per_frame;
        lines_per_frame_sync <= sync_lines_meta;
    end
end

axi_stream_to_video_timing
#(
    .LINES_PER_FRAME(C_LINES_PER_FRAME_INIT)
)
u_video_timing
(
    .clk_in                (clk_in),
    .reset_n               (reset_n),
    .s_axis_video_tlast    (s_axis_video_tlast),
    .s_axis_video_tuser    (s_axis_video_tuser),
    .s_axis_video_tvalid   (s_axis_video_tvalid),
    .s_axis_video_tready   (s_axis_video_tready),
    .lines_per_frame       (lines_per_frame_sync),
    .vt_fsync              (vt_fsync),
    .vt_lsync              (vt_lsync)
);

endmodule
