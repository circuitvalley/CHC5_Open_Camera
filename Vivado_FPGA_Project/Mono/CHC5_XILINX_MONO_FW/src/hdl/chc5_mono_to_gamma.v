/*
 * chc5_mono_to_gamma.v - places a mono pixel on one channel of the gamma bus
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module chc5_mono_to_gamma #(
    parameter integer IN_PIXEL_WIDTH   = 12,
    parameter integer OUT_COMP_WIDTH   = 8,
    parameter integer CHANNEL          = 1,
    parameter integer PIXELS_PER_CLOCK = 4
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXIS:M_AXIS" *)
    input  wire                          aclk,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TDATA" *)
    (* X_INTERFACE_PARAMETER = "HAS_TKEEP 1, HAS_TSTRB 0, HAS_TREADY 1, HAS_TLAST 1, TUSER_WIDTH 1" *)
    input  wire [((PIXELS_PER_CLOCK*IN_PIXEL_WIDTH+7)/8)*8-1:0] s_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TKEEP" *)
    input  wire [(PIXELS_PER_CLOCK*IN_PIXEL_WIDTH+7)/8-1:0]     s_axis_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TVALID" *)
    input  wire                          s_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TREADY" *)
    output wire                          s_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TLAST" *)
    input  wire                          s_axis_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TUSER" *)
    input  wire [0:0]                    s_axis_tuser,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TDATA" *)
    (* X_INTERFACE_PARAMETER = "HAS_TKEEP 1, HAS_TSTRB 0, HAS_TREADY 1, HAS_TLAST 1, TUSER_WIDTH 1" *)
    output wire [((PIXELS_PER_CLOCK*3*OUT_COMP_WIDTH+7)/8)*8-1:0] m_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TKEEP" *)
    output wire [(PIXELS_PER_CLOCK*3*OUT_COMP_WIDTH+7)/8-1:0]     m_axis_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TVALID" *)
    output wire                          m_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TREADY" *)
    input  wire                          m_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TLAST" *)
    output wire                          m_axis_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TUSER" *)
    output wire [0:0]                    m_axis_tuser
);

    localparam integer IW  = IN_PIXEL_WIDTH;
    localparam integer OW  = OUT_COMP_WIDTH;
    localparam integer PPC = PIXELS_PER_CLOCK;

    localparam integer M_ACTIVE_W = PPC*3*OW;
    localparam integer M_TDATA_W  = ((M_ACTIVE_W+7)/8)*8;
    localparam integer M_TKEEP_W  = (M_ACTIVE_W+7)/8;

    localparam integer LANE = (CHANNEL == 0) ? 2 :
                              (CHANNEL == 1) ? 0 : 1;

    localparam integer REP = (OW + IW - 1) / IW;

    assign s_axis_tready = m_axis_tready;
    assign m_axis_tvalid = s_axis_tvalid;
    assign m_axis_tlast  = s_axis_tlast;
    assign m_axis_tuser  = s_axis_tuser;
    assign m_axis_tkeep  = {M_TKEEP_W{1'b1}};

    genvar k, c;
    generate
        for (k = 0; k < PPC; k = k + 1) begin : gen_pixel
            wire [IW-1:0]     mono = s_axis_tdata[k*IW +: IW];
            wire [REP*IW-1:0] rep  = {REP{mono}};
            wire [OW-1:0]     grey = rep[REP*IW-1 -: OW];

            for (c = 0; c < 3; c = c + 1) begin : gen_comp
                assign m_axis_tdata[(k*3 + c)*OW +: OW] =
                    (c == LANE) ? grey : {OW{1'b0}};
            end
        end

        if (M_TDATA_W > M_ACTIVE_W) begin : gen_pad
            assign m_axis_tdata[M_TDATA_W-1 : M_ACTIVE_W] =
                       {(M_TDATA_W - M_ACTIVE_W){1'b0}};
        end
    endgenerate

endmodule
