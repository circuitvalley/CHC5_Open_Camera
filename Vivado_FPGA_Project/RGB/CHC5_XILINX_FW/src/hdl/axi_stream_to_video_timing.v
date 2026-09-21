/*
 * axi_stream_to_video_timing.v - AXI4-Stream to frame/line sync video timing converter
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1ns / 1ps
module axi_stream_to_video_timing
#(
    parameter integer LINES_PER_FRAME = 1080
)
(
    clk_in,
    reset_n,
    s_axis_video_tlast,
    s_axis_video_tuser,
    s_axis_video_tvalid,
    s_axis_video_tready,
    lines_per_frame,
    vt_fsync,
    vt_lsync
);

input         clk_in;
input         reset_n;
input         s_axis_video_tlast;
input         s_axis_video_tuser;
input         s_axis_video_tvalid;
output reg    s_axis_video_tready;
input  [12:0] lines_per_frame;
output reg    vt_fsync;
output        vt_lsync;

reg        last_s_axis_video_tuser;
reg        last_s_axis_video_tlast;
reg [7:0]  hsync_counter;
reg [12:0] line_counter;
reg [2:0]  fsync_deassert_counter;
reg        fsync_deassert_pending;

assign vt_lsync = s_axis_video_tready & s_axis_video_tvalid;

always @(posedge clk_in)
begin
    if (!reset_n)
    begin
        vt_fsync                 <= 1'b0;
        hsync_counter            <= 8'd0;
        s_axis_video_tready      <= 1'b1;
        last_s_axis_video_tuser  <= 1'b0;
        last_s_axis_video_tlast  <= 1'b0;
        line_counter             <= 13'd0;
        fsync_deassert_counter   <= 3'd0;
        fsync_deassert_pending   <= 1'b0;
    end
    else
    begin
        last_s_axis_video_tuser <= s_axis_video_tuser;
        last_s_axis_video_tlast <= s_axis_video_tlast;

        hsync_counter <= hsync_counter + 1'b1;

        if (!last_s_axis_video_tuser && s_axis_video_tuser)
        begin
            vt_fsync               <= 1'b1;
            line_counter           <= 13'd0;
            fsync_deassert_pending <= 1'b0;
            fsync_deassert_counter <= 3'd0;
        end

        if (!last_s_axis_video_tlast && s_axis_video_tlast && s_axis_video_tvalid)
        begin
            line_counter <= line_counter + 1'b1;

            if ((line_counter + 1'b1) >= lines_per_frame)
            begin
                fsync_deassert_pending <= 1'b1;
                fsync_deassert_counter <= 3'd0;
            end
        end

        if (fsync_deassert_pending)
        begin
            if (fsync_deassert_counter >= 3'd3)
            begin
                vt_fsync               <= 1'b0;
                fsync_deassert_pending <= 1'b0;
            end
            else
            begin
                fsync_deassert_counter <= fsync_deassert_counter + 1'b1;
            end
        end

        if (!last_s_axis_video_tlast && s_axis_video_tlast)
        begin
            hsync_counter       <= 8'd0;
            s_axis_video_tready <= 1'b0;
        end
        else
        begin
            if (hsync_counter[2])
            begin
                s_axis_video_tready <= 1'b1;
            end
        end

    end
end

endmodule
