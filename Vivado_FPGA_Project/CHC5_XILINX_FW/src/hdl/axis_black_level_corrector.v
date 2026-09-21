/*
 * axis_black_level_corrector.v - AXI4-Stream black-level subtraction with saturation
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1ns / 1ps

module axis_black_level_corrector #(
    parameter integer PIXEL_WIDTH    = 10,
    parameter integer PIXELS_PER_CLK = 2
) (
    input  wire                                     aclk,
    input  wire                                     aresetn,

    input  wire [PIXEL_WIDTH-1:0]                   black_level,

    input  wire [((PIXELS_PER_CLK*PIXEL_WIDTH+7)/8)*8-1:0]  s_axis_video_tdata,
    input  wire                                     s_axis_video_tvalid,
    output wire                                     s_axis_video_tready,
    input  wire                                     s_axis_video_tlast,
    input  wire [0:0]                               s_axis_video_tuser,
    input  wire [((PIXELS_PER_CLK*PIXEL_WIDTH+7)/8)-1:0]  s_axis_video_tkeep,
    input  wire [0:0]                               s_axis_video_tdest,
    input  wire [0:0]                               s_axis_video_tid,

    output wire [((PIXELS_PER_CLK*PIXEL_WIDTH+7)/8)*8-1:0]  m_axis_video_tdata,
    output wire                                     m_axis_video_tvalid,
    input  wire                                     m_axis_video_tready,
    output wire                                     m_axis_video_tlast,
    output wire [0:0]                               m_axis_video_tuser,
    output wire [((PIXELS_PER_CLK*PIXEL_WIDTH+7)/8)-1:0]  m_axis_video_tkeep,
    output wire [0:0]                               m_axis_video_tdest,
    output wire [0:0]                               m_axis_video_tid
);

    localparam integer RAW_WIDTH   = PIXELS_PER_CLK * PIXEL_WIDTH;
    localparam integer TDATA_WIDTH = ((RAW_WIDTH + 7) / 8) * 8;
    localparam integer TKEEP_WIDTH = TDATA_WIDTH / 8;
    localparam integer PAD_BITS    = TDATA_WIDTH - RAW_WIDTH;

    reg  [TDATA_WIDTH-1:0]  r_tdata;
    reg                     r_tvalid;
    reg                     r_tlast;
    reg  [0:0]              r_tuser;
    reg  [TKEEP_WIDTH-1:0]  r_tkeep;
    reg  [0:0]              r_tdest;
    reg  [0:0]              r_tid;

    wire s_ready = !r_tvalid || m_axis_video_tready;
    wire s_xfer  = s_axis_video_tvalid && s_ready;

    reg [PIXEL_WIDTH-1:0] bl_latched;

    always @(posedge aclk) begin
        if (!aresetn)
            bl_latched <= {PIXEL_WIDTH{1'b0}};
        else if (s_xfer && s_axis_video_tuser[0])
            bl_latched <= black_level;
    end

    wire [RAW_WIDTH-1:0] subtracted_raw;

    genvar p;
    generate
        for (p = 0; p < PIXELS_PER_CLK; p = p + 1) begin : gen_pixel

            wire [PIXEL_WIDTH-1:0] pixel_in;
            wire [PIXEL_WIDTH  :0] diff;
            wire [PIXEL_WIDTH-1:0] pixel_out;

            assign pixel_in  = s_axis_video_tdata[p*PIXEL_WIDTH +: PIXEL_WIDTH];
            assign diff      = {1'b0, pixel_in} - {1'b0, bl_latched};
            assign pixel_out = diff[PIXEL_WIDTH] ? {PIXEL_WIDTH{1'b0}} : diff[PIXEL_WIDTH-1:0];

            assign subtracted_raw[p*PIXEL_WIDTH +: PIXEL_WIDTH] = pixel_out;

        end
    endgenerate

    wire [TDATA_WIDTH-1:0] subtracted;
    generate
        if (PAD_BITS > 0) begin : gen_pad
            assign subtracted = {{PAD_BITS{1'b0}}, subtracted_raw};
        end else begin : gen_nopad
            assign subtracted = subtracted_raw;
        end
    endgenerate

    always @(posedge aclk) begin
        if (!aresetn) begin
            r_tvalid <= 1'b0;
            r_tdata  <= {TDATA_WIDTH{1'b0}};
            r_tlast  <= 1'b0;
            r_tuser  <= 1'b0;
            r_tkeep  <= {TKEEP_WIDTH{1'b0}};
            r_tdest  <= 1'b0;
            r_tid    <= 1'b0;
        end else begin
            if (s_ready)
                r_tvalid <= s_axis_video_tvalid;
            if (s_xfer) begin
                r_tdata  <= subtracted;
                r_tlast  <= s_axis_video_tlast;
                r_tuser  <= s_axis_video_tuser;
                r_tkeep  <= s_axis_video_tkeep;
                r_tdest  <= s_axis_video_tdest;
                r_tid    <= s_axis_video_tid;
            end
        end
    end

    assign m_axis_video_tdata  = r_tdata;
    assign m_axis_video_tvalid = r_tvalid;
    assign m_axis_video_tlast  = r_tlast;
    assign m_axis_video_tuser  = r_tuser;
    assign m_axis_video_tkeep  = r_tkeep;
    assign m_axis_video_tdest  = r_tdest;
    assign m_axis_video_tid    = r_tid;
    assign s_axis_video_tready = s_ready;

endmodule
