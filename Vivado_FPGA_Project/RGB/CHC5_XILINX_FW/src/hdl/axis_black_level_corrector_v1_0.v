/*
 * axis_black_level_corrector_v1_0.v - AXI4-Lite wrapper for axis_black_level_corrector
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1ns / 1ps

module axis_black_level_corrector_v1_0 #(
    parameter integer PIXEL_WIDTH    = 10,
    parameter integer PIXELS_PER_CLK = 2
) (
    input  wire        s_axi_aclk,
    input  wire        s_axi_aresetn,
    input  wire [3:0]  s_axi_awaddr,
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
    input  wire [3:0]  s_axi_araddr,
    input  wire [2:0]  s_axi_arprot,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output wire        s_axi_rvalid,
    input  wire        s_axi_rready,

    input  wire        aclk,
    input  wire        aresetn,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis_video TDATA" *)
    (* X_INTERFACE_PARAMETER = "HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1, HAS_TSTRB 0" *)
    input  wire [((PIXELS_PER_CLK*PIXEL_WIDTH+7)/8)*8-1:0]  s_axis_video_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis_video TVALID" *)
    input  wire        s_axis_video_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis_video TREADY" *)
    output wire        s_axis_video_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis_video TLAST" *)
    input  wire        s_axis_video_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis_video TUSER" *)
    input  wire [0:0]  s_axis_video_tuser,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis_video TKEEP" *)
    input  wire [((PIXELS_PER_CLK*PIXEL_WIDTH+7)/8)-1:0]  s_axis_video_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis_video TDEST" *)
    input  wire [0:0]  s_axis_video_tdest,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 s_axis_video TID" *)
    input  wire [0:0]  s_axis_video_tid,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis_video TDATA" *)
    (* X_INTERFACE_PARAMETER = "HAS_TKEEP 1, HAS_TLAST 1, HAS_TREADY 1, HAS_TSTRB 0" *)
    output wire [((PIXELS_PER_CLK*PIXEL_WIDTH+7)/8)*8-1:0]  m_axis_video_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis_video TVALID" *)
    output wire        m_axis_video_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis_video TREADY" *)
    input  wire        m_axis_video_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis_video TLAST" *)
    output wire        m_axis_video_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis_video TUSER" *)
    output wire [0:0]  m_axis_video_tuser,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis_video TKEEP" *)
    output wire [((PIXELS_PER_CLK*PIXEL_WIDTH+7)/8)-1:0]  m_axis_video_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis_video TDEST" *)
    output wire [0:0]  m_axis_video_tdest,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis_video TID" *)
    output wire [0:0]  m_axis_video_tid
);

reg        axi_awready;
reg        axi_wready;
reg        axi_bvalid;
reg        axi_arready;
reg        axi_rvalid;
reg [31:0] axi_rdata;

localparam [31:0] VERSION_VAL = 32'h424C_0100;
localparam [31:0] CONFIG_VAL  = {20'd0, PIXELS_PER_CLK[3:0], PIXEL_WIDTH[7:0]};

localparam [1:0] A_BLACK_LEVEL = 2'd0;
localparam [1:0] A_VERSION     = 2'd1;
localparam [1:0] A_CONFIG      = 2'd2;
localparam [1:0] A_SCRATCH     = 2'd3;

reg [13:0] slv_black_level;
reg [31:0] slv_scratch;

assign s_axi_awready = axi_awready;
assign s_axi_wready  = axi_wready;
assign s_axi_bresp   = 2'b00;
assign s_axi_bvalid  = axi_bvalid;
assign s_axi_arready = axi_arready;
assign s_axi_rdata   = axi_rdata;
assign s_axi_rresp   = 2'b00;
assign s_axi_rvalid  = axi_rvalid;

always @(posedge s_axi_aclk) begin
    if (!s_axi_aresetn)
        axi_awready <= 1'b0;
    else if (~axi_awready && s_axi_awvalid && s_axi_wvalid)
        axi_awready <= 1'b1;
    else
        axi_awready <= 1'b0;
end

always @(posedge s_axi_aclk) begin
    if (!s_axi_aresetn)
        axi_wready <= 1'b0;
    else if (~axi_wready && s_axi_awvalid && s_axi_wvalid)
        axi_wready <= 1'b1;
    else
        axi_wready <= 1'b0;
end

always @(posedge s_axi_aclk) begin
    if (!s_axi_aresetn) begin
        slv_black_level <= 14'd0;
        slv_scratch     <= 32'd0;
    end
    else if (axi_awready && s_axi_awvalid && axi_wready && s_axi_wvalid) begin
        case (s_axi_awaddr[3:2])
        A_BLACK_LEVEL: begin
            if (s_axi_wstrb[0])
                slv_black_level[7:0]  <= s_axi_wdata[7:0];
            if (s_axi_wstrb[1])
                slv_black_level[13:8] <= s_axi_wdata[13:8];
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

always @(posedge s_axi_aclk) begin
    if (!s_axi_aresetn)
        axi_bvalid <= 1'b0;
    else if (axi_awready && s_axi_awvalid && axi_wready && s_axi_wvalid && ~axi_bvalid)
        axi_bvalid <= 1'b1;
    else if (s_axi_bready && axi_bvalid)
        axi_bvalid <= 1'b0;
end

always @(posedge s_axi_aclk) begin
    if (!s_axi_aresetn)
        axi_arready <= 1'b0;
    else if (~axi_arready && s_axi_arvalid && ~axi_rvalid)
        axi_arready <= 1'b1;
    else
        axi_arready <= 1'b0;
end

always @(posedge s_axi_aclk) begin
    if (!s_axi_aresetn) begin
        axi_rvalid <= 1'b0;
        axi_rdata  <= 32'd0;
    end
    else if (axi_arready && s_axi_arvalid && ~axi_rvalid) begin
        axi_rvalid <= 1'b1;
        case (s_axi_araddr[3:2])
        A_BLACK_LEVEL: axi_rdata <= {18'd0, slv_black_level};
        A_VERSION:     axi_rdata <= VERSION_VAL;
        A_CONFIG:      axi_rdata <= CONFIG_VAL;
        A_SCRATCH:     axi_rdata <= slv_scratch;
        endcase
    end
    else if (axi_rvalid && s_axi_rready) begin
        axi_rvalid <= 1'b0;
    end
end

(* ASYNC_REG = "TRUE" *) reg [13:0] sync_bl_meta;
(* ASYNC_REG = "TRUE" *) reg [13:0] black_level_sync;

always @(posedge aclk) begin
    if (!aresetn) begin
        sync_bl_meta    <= 14'd0;
        black_level_sync <= 14'd0;
    end
    else begin
        sync_bl_meta    <= slv_black_level;
        black_level_sync <= sync_bl_meta;
    end
end

localparam integer TDW = ((PIXELS_PER_CLK*PIXEL_WIDTH+7)/8)*8;
localparam integer TKW = ((PIXELS_PER_CLK*PIXEL_WIDTH+7)/8);

wire [TDW+1:0] ci_pack, co_pack;
wire [TKW-1:0] ci_tkeep, co_tkeep;
wire           ci_tvalid, ci_tready, ci_tlast;
wire           co_tvalid, co_tready, co_tlast;
wire [0:0]     ci_tuser,  co_tuser;

wire [TDW-1:0] ci_tdata = ci_pack[TDW+1:2];
wire [0:0]     ci_tdest = ci_pack[1];
wire [0:0]     ci_tid   = ci_pack[0];

wire [TDW-1:0] co_tdata;
wire [0:0]     co_tdest, co_tid;
assign co_pack = {co_tdata, co_tdest, co_tid};

chc5_axis_skid #(.DW(TDW+2), .KW(TKW)) u_skid_in (
    .aclk(aclk), .aresetn(aresetn),
    .s_tdata ({s_axis_video_tdata, s_axis_video_tdest, s_axis_video_tid}),
    .s_tkeep (s_axis_video_tkeep),
    .s_tvalid(s_axis_video_tvalid),
    .s_tready(s_axis_video_tready),
    .s_tlast (s_axis_video_tlast),
    .s_tuser (s_axis_video_tuser),
    .m_tdata (ci_pack), .m_tkeep(ci_tkeep), .m_tvalid(ci_tvalid),
    .m_tready(ci_tready), .m_tlast(ci_tlast), .m_tuser(ci_tuser));

axis_black_level_corrector #(
    .PIXEL_WIDTH    (PIXEL_WIDTH),
    .PIXELS_PER_CLK (PIXELS_PER_CLK)
) u_blc (
    .aclk                (aclk),
    .aresetn             (aresetn),
    .black_level         (black_level_sync[PIXEL_WIDTH-1:0]),
    .s_axis_video_tdata  (ci_tdata),
    .s_axis_video_tvalid (ci_tvalid),
    .s_axis_video_tready (ci_tready),
    .s_axis_video_tlast  (ci_tlast),
    .s_axis_video_tuser  (ci_tuser),
    .s_axis_video_tkeep  (ci_tkeep),
    .s_axis_video_tdest  (ci_tdest),
    .s_axis_video_tid    (ci_tid),
    .m_axis_video_tdata  (co_tdata),
    .m_axis_video_tvalid (co_tvalid),
    .m_axis_video_tready (co_tready),
    .m_axis_video_tlast  (co_tlast),
    .m_axis_video_tuser  (co_tuser),
    .m_axis_video_tkeep  (co_tkeep),
    .m_axis_video_tdest  (co_tdest),
    .m_axis_video_tid    (co_tid)
);

wire [TDW+1:0] mo_pack;
chc5_axis_skid #(.DW(TDW+2), .KW(TKW)) u_skid_out (
    .aclk(aclk), .aresetn(aresetn),
    .s_tdata (co_pack), .s_tkeep(co_tkeep), .s_tvalid(co_tvalid),
    .s_tready(co_tready), .s_tlast(co_tlast), .s_tuser(co_tuser),
    .m_tdata (mo_pack), .m_tkeep(m_axis_video_tkeep), .m_tvalid(m_axis_video_tvalid),
    .m_tready(m_axis_video_tready), .m_tlast(m_axis_video_tlast),
    .m_tuser (m_axis_video_tuser));

assign m_axis_video_tdata = mo_pack[TDW+1:2];
assign m_axis_video_tdest = mo_pack[1];
assign m_axis_video_tid   = mo_pack[0];

endmodule
