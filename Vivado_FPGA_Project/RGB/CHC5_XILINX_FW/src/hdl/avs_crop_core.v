/*
 * avs_crop_core.v - crop and frame-decimation datapath for axis_video_subsample
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module avs_crop_core #(
    parameter integer PPC    = 2,
    parameter integer FORMAT = 0
)(
    input  wire                 aclk,
    input  wire                 aresetn,

    input  wire [PPC*(24-8*FORMAT)-1:0] s_axis_tdata,
    input  wire [PPC*(3-FORMAT)-1:0]   s_axis_tkeep,
    input  wire                 s_axis_tvalid,
    output wire                 s_axis_tready,
    input  wire                 s_axis_tlast,
    input  wire [0:0]           s_axis_tuser,

    output wire [PPC*(24-8*FORMAT)-1:0] m_axis_tdata,
    output wire [PPC*(3-FORMAT)-1:0]   m_axis_tkeep,
    output wire                 m_axis_tvalid,
    input  wire                 m_axis_tready,
    output wire                 m_axis_tlast,
    output wire [0:0]           m_axis_tuser,

    input  wire [12:0]          reg_left,
    input  wire [12:0]          reg_top,
    input  wire [7:0]           reg_frame_num,

    input  wire [12:0]          h_right,
    input  wire [12:0]          v_bottom,
    input  wire [12:0]          h_last_pix,
    input  wire [7:0]           eff_frame_den_m1
);

    reg [12:0] pixel_cnt;
    reg [12:0] line_cnt;

    wire [12:0] cur_pixel = s_axis_tuser[0] ? 13'd0 : pixel_cnt;
    wire [12:0] cur_line  = s_axis_tuser[0] ? 13'd0 : line_cnt;

    wire h_active        = (cur_pixel >= reg_left) && (cur_pixel < h_right);
    wire v_active        = (cur_line  >= reg_top)  && (cur_line  < v_bottom);
    wire h_last          = (cur_pixel >= h_last_pix);
    wire first_out_pixel = (cur_line  == reg_top)  && (cur_pixel == reg_left);

    reg [7:0] frame_cnt;
    reg       frame_active_held;

    wire frame_active_at_sof   = (frame_cnt < reg_frame_num);
    wire frame_active_for_ready = s_axis_tuser[0] ? frame_active_at_sof
                                                   : frame_active_held;

    wire in_region = h_active & v_active & frame_active_for_ready;

    assign s_axis_tready = in_region ? m_axis_tready : 1'b1;

    wire s_beat = s_axis_tvalid & s_axis_tready;
    wire is_sof = s_beat & s_axis_tuser[0];

    always @(posedge aclk) begin
        if (!aresetn) begin
            pixel_cnt <= 13'd0;
            line_cnt  <= 13'd0;
        end else if (s_beat) begin
            if (s_axis_tuser[0]) begin
                pixel_cnt <= s_axis_tlast ? 13'd0 : PPC[12:0];
                line_cnt  <= s_axis_tlast ? 13'd1 : 13'd0;
            end else if (s_axis_tlast) begin
                pixel_cnt <= 13'd0;
                line_cnt  <= line_cnt + 13'd1;
            end else begin
                pixel_cnt <= pixel_cnt + PPC[12:0];
            end
        end
    end

    always @(posedge aclk) begin
        if (!aresetn)
            frame_active_held <= 1'b0;
        else if (is_sof)
            frame_active_held <= frame_active_at_sof;
    end

    always @(posedge aclk) begin
        if (!aresetn) begin
            frame_cnt <= 8'd0;
        end else if (is_sof) begin
            if (frame_cnt >= eff_frame_den_m1)
                frame_cnt <= 8'd0;
            else
                frame_cnt <= frame_cnt + 8'd1;
        end
    end

    wire out_active = s_axis_tvalid & h_active & v_active & frame_active_for_ready;

    assign m_axis_tdata  = s_axis_tdata;
    assign m_axis_tkeep  = s_axis_tkeep;
    assign m_axis_tvalid = out_active;
    assign m_axis_tlast  = out_active & h_last;
    assign m_axis_tuser  = out_active & first_out_pixel;

endmodule
