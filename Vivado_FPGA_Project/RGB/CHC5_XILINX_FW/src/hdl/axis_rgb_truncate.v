/*
 * axis_rgb_truncate.v - AXI4-Stream RGB bit-depth truncator
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1ns / 1ps

module axis_rgb_truncate #(
    parameter integer INPUT_COMPONENT_WIDTH = 12,
    parameter integer PIXELS_PER_CLOCK      = 2
) (
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axis:m_axis" *)
    input  wire                      aclk,

    input  wire [((PIXELS_PER_CLOCK*3*INPUT_COMPONENT_WIDTH+7)/8)*8-1:0] s_axis_tdata,
    input  wire [(PIXELS_PER_CLOCK*3*INPUT_COMPONENT_WIDTH+7)/8-1:0]     s_axis_tkeep,
    input  wire                      s_axis_tlast,
    input  wire                      s_axis_tuser,
    input  wire                      s_axis_tvalid,
    output wire                      s_axis_tready,
    input  wire                      s_axis_tid,
    input  wire                      s_axis_tdest,

    output wire [PIXELS_PER_CLOCK*3*8-1:0]  m_axis_tdata,
    output wire [PIXELS_PER_CLOCK*3-1:0]    m_axis_tkeep,
    output wire                      m_axis_tlast,
    output wire                      m_axis_tuser,
    output wire                      m_axis_tvalid,
    input  wire                      m_axis_tready,
    output wire                      m_axis_tid,
    output wire                      m_axis_tdest
);

    assign s_axis_tready = m_axis_tready;
    assign m_axis_tvalid = s_axis_tvalid;
    assign m_axis_tlast  = s_axis_tlast;
    assign m_axis_tuser  = s_axis_tuser;
    assign m_axis_tid    = s_axis_tid;
    assign m_axis_tdest  = s_axis_tdest;
    assign m_axis_tkeep  = {(PIXELS_PER_CLOCK*3){1'b1}};

    genvar c;
    generate
        for (c = 0; c < PIXELS_PER_CLOCK * 3; c = c + 1) begin : gen_truncate
            assign m_axis_tdata[c*8 +: 8] =
                s_axis_tdata[c*INPUT_COMPONENT_WIDTH + (INPUT_COMPONENT_WIDTH - 8) +: 8];
        end
    endgenerate

endmodule
