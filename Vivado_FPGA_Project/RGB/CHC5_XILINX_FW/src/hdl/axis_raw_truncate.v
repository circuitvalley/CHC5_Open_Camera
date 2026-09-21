/*
 * axis_raw_truncate.v - AXI4-Stream RAW/Bayer bit-depth truncator
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1ns / 1ps

module axis_raw_truncate #(
    parameter integer INPUT_PIXEL_WIDTH  = 14,
    parameter integer OUTPUT_PIXEL_WIDTH = 12,
    parameter integer PIXELS_PER_CLOCK   = 2
) (
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axis:m_axis" *)
    input  wire                      aclk,

    input  wire [((PIXELS_PER_CLOCK*INPUT_PIXEL_WIDTH+7)/8)*8-1:0]  s_axis_tdata,
    input  wire [(PIXELS_PER_CLOCK*INPUT_PIXEL_WIDTH+7)/8-1:0]      s_axis_tkeep,
    input  wire                      s_axis_tlast,
    input  wire                      s_axis_tuser,
    input  wire                      s_axis_tvalid,
    output wire                      s_axis_tready,
    input  wire                      s_axis_tid,
    input  wire                      s_axis_tdest,

    output wire [((PIXELS_PER_CLOCK*OUTPUT_PIXEL_WIDTH+7)/8)*8-1:0] m_axis_tdata,
    output wire [(PIXELS_PER_CLOCK*OUTPUT_PIXEL_WIDTH+7)/8-1:0]     m_axis_tkeep,
    output wire                      m_axis_tlast,
    output wire                      m_axis_tuser,
    output wire                      m_axis_tvalid,
    input  wire                      m_axis_tready,
    output wire                      m_axis_tid,
    output wire                      m_axis_tdest
);

    localparam integer IN_PACKED_BITS  = PIXELS_PER_CLOCK * INPUT_PIXEL_WIDTH;
    localparam integer OUT_PACKED_BITS = PIXELS_PER_CLOCK * OUTPUT_PIXEL_WIDTH;
    localparam integer OUT_BYTES       = (OUT_PACKED_BITS + 7) / 8;
    localparam integer OUT_TDATA_BITS  = OUT_BYTES * 8;
    localparam integer OUT_PAD_BITS    = OUT_TDATA_BITS - OUT_PACKED_BITS;

    assign s_axis_tready = m_axis_tready;
    assign m_axis_tvalid = s_axis_tvalid;
    assign m_axis_tlast  = s_axis_tlast;
    assign m_axis_tuser  = s_axis_tuser;
    assign m_axis_tid    = s_axis_tid;
    assign m_axis_tdest  = s_axis_tdest;
    assign m_axis_tkeep  = {OUT_BYTES{1'b1}};

    wire [OUT_PACKED_BITS-1:0] packed_out;

    genvar p;
    generate
        for (p = 0; p < PIXELS_PER_CLOCK; p = p + 1) begin : gen_truncate
            assign packed_out[p*OUTPUT_PIXEL_WIDTH +: OUTPUT_PIXEL_WIDTH] =
                s_axis_tdata[p*INPUT_PIXEL_WIDTH +
                             (INPUT_PIXEL_WIDTH - OUTPUT_PIXEL_WIDTH) +:
                             OUTPUT_PIXEL_WIDTH];
        end

        if (OUT_PAD_BITS == 0) begin : gen_no_pad
            assign m_axis_tdata = packed_out;
        end else begin : gen_pad
            assign m_axis_tdata = { {OUT_PAD_BITS{1'b0}}, packed_out };
        end
    endgenerate

endmodule
