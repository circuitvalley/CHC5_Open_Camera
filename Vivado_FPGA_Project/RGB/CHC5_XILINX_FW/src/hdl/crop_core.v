/*
 * crop_core.v - crop window datapath for chc5_axis_crop
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module crop_core #(
    parameter integer PPC             = 4,
    parameter integer COMPONENT_WIDTH = 12,
    parameter integer DIM_W           = 16,
    parameter integer SEL_W           = (PPC <= 1) ? 1 : (PPC <= 2) ? 2 : (PPC <= 4) ? 3 : 4,
    parameter integer TDW             = PPC * COMPONENT_WIDTH,
    parameter integer TKW             = (TDW + 7) / 8
)(
    input  wire                    aclk,
    input  wire                    aresetn,

    input  wire [TDW-1:0]          s_tdata,
    input  wire [TKW-1:0]          s_tkeep,
    input  wire                    s_tvalid,
    output wire                    s_tready,
    input  wire                    s_tlast,
    input  wire [0:0]              s_tuser,

    output wire [TDW-1:0]          m_tdata,
    output wire [TKW-1:0]          m_tkeep,
    output wire                    m_tvalid,
    input  wire                    m_tready,
    output wire                    m_tlast,
    output wire [0:0]              m_tuser,

    input  wire                    cfg_enable,
    input  wire                    cfg_bypass,
    input  wire                    cfg_drop_until_sof,
    input  wire [DIM_W-1:0]        cfg_x,
    input  wire [DIM_W-1:0]        cfg_y,
    input  wire [DIM_W-1:0]        cfg_w,
    input  wire [DIM_W-1:0]        cfg_h,
    input  wire [DIM_W-1:0]        cfg_y_end,
    input  wire [DIM_W-1:0]        cfg_start_beat,
    input  wire [DIM_W-1:0]        cfg_nbeats_m1,
    input  wire [SEL_W-1:0]        cfg_sel,

    output wire [DIM_W-1:0]        act_x,
    output wire [DIM_W-1:0]        act_y,
    output wire [DIM_W-1:0]        act_w,
    output wire [DIM_W-1:0]        act_h,
    output wire [DIM_W-1:0]        geom_w,
    output wire [DIM_W-1:0]        geom_h,
    output wire [31:0]             frame_cnt,
    output wire                    sts_short_line,
    output wire                    sts_window_oob,
    output wire                    sts_sof_resync
);

    localparam integer LOG2PPC  = (PPC <= 1) ? 0 : (PPC <= 2) ? 1 :
                                  (PPC <= 4) ? 2 : 3;
    localparam [DIM_W-1:0] ONES = {DIM_W{1'b1}};
    localparam [DIM_W-1:0] ZERO = {DIM_W{1'b0}};
    localparam [DIM_W-1:0] ONE  = {{(DIM_W-1){1'b0}}, 1'b1};

    reg  [DIM_W-1:0] beat_idx;
    reg  [DIM_W-1:0] line_cnt;
    reg  [DIM_W-1:0] out_beat_cnt;
    reg              line_done;
    reg              armed;
    reg              sof_sent;
    reg  [TDW-1:0]   prev_data;

    reg  [DIM_W-1:0] act_x_r, act_y_r, act_w_r, act_h_r;
    reg  [DIM_W-1:0] act_y_end_r, act_start_beat_r, act_nbeats_m1_r;
    reg  [SEL_W-1:0] act_sel_r;

    reg  [DIM_W-1:0] geom_w_line;
    reg  [DIM_W-1:0] geom_w_r, geom_h_r;
    reg  [31:0]      frame_cnt_r;

    wire sof = s_tuser[0];

    wire [DIM_W-1:0] cur_beat     = sof ? ZERO : beat_idx;
    wire [DIM_W-1:0] cur_line     = sof ? ZERO : line_cnt;
    wire [DIM_W-1:0] cur_out_beat = sof ? ZERO : out_beat_cnt;
    wire             cur_done     = sof ? 1'b0 : line_done;
    wire             cur_sof_sent = sof ? 1'b0 : sof_sent;

    wire [DIM_W-1:0] eff_y        = sof ? cfg_y          : act_y_r;
    wire [DIM_W-1:0] eff_y_end    = sof ? cfg_y_end      : act_y_end_r;
    wire [DIM_W-1:0] eff_start    = sof ? cfg_start_beat : act_start_beat_r;
    wire [DIM_W-1:0] eff_nbeats   = sof ? cfg_nbeats_m1  : act_nbeats_m1_r;
    wire [SEL_W-1:0] eff_sel      = sof ? cfg_sel        : act_sel_r;

    wire eff_armed = ~cfg_drop_until_sof | armed | sof;

    wire v_active  = (cur_line >= eff_y) && (cur_line < eff_y_end);
    wire h_started = (cur_beat >= eff_start);
    wire last_out  = (cur_out_beat == eff_nbeats);

    wire in_window = v_active && h_started && !cur_done && eff_armed;

    assign s_tready = ~cfg_enable ? 1'b1
                    :  cfg_bypass ? m_tready
                    : (in_window  ? m_tready : 1'b1);

    assign m_tvalid = ~cfg_enable ? 1'b0
                    :  cfg_bypass ? s_tvalid
                    : (s_tvalid & in_window);

    wire s_beat    = s_tvalid & s_tready & cfg_enable;
    wire emit_beat = s_beat & in_window & ~cfg_bypass;

    wire [2*TDW-1:0] win      = {s_tdata, prev_data};
    wire [15:0]      shift_by = {{(16-SEL_W){1'b0}}, eff_sel} * COMPONENT_WIDTH;
    wire [TDW-1:0]   cropped  = win[shift_by +: TDW];

    assign m_tdata = cfg_bypass ? s_tdata : cropped;

    assign m_tkeep = cfg_bypass ? s_tkeep : {TKW{1'b1}};

    assign m_tlast = cfg_bypass ? s_tlast
                                : (m_tvalid & (last_out | s_tlast));

    assign m_tuser = cfg_bypass ? s_tuser[0]
                                : (m_tvalid & ~cur_sof_sent);

    always @(posedge aclk) begin
        if (!aresetn) begin
            beat_idx <= ZERO;
            line_cnt <= ZERO;
        end else if (s_beat) begin
            if (sof) begin
                beat_idx <= s_tlast ? ZERO : ONE;
                line_cnt <= s_tlast ? ONE  : ZERO;
            end else if (s_tlast) begin
                beat_idx <= ZERO;
                line_cnt <= line_cnt + ONE;
            end else begin
                beat_idx <= beat_idx + ONE;
            end
        end
    end

    always @(posedge aclk) begin
        if (!aresetn) begin
            out_beat_cnt <= ZERO;
            line_done    <= 1'b0;
        end else if (s_beat) begin
            if (sof) begin
                if (s_tlast) begin
                    out_beat_cnt <= ZERO;
                    line_done    <= 1'b0;
                end else begin
                    out_beat_cnt <= emit_beat ? ONE : ZERO;
                    line_done    <= emit_beat & last_out;
                end
            end else if (s_tlast) begin
                out_beat_cnt <= ZERO;
                line_done    <= 1'b0;
            end else if (emit_beat) begin
                out_beat_cnt <= out_beat_cnt + ONE;
                line_done    <= last_out;
            end
        end
    end

    always @(posedge aclk) begin
        if (!aresetn) prev_data <= {TDW{1'b0}};
        else if (s_beat) prev_data <= s_tdata;
    end

    always @(posedge aclk) begin
        if (!aresetn)          armed <= 1'b0;
        else if (~cfg_enable)  armed <= 1'b0;
        else if (s_beat & sof) armed <= 1'b1;
    end

    always @(posedge aclk) begin
        if (!aresetn) begin
            sof_sent <= 1'b0;
        end else if (s_beat) begin
            if (sof)             sof_sent <= emit_beat;
            else if (emit_beat)  sof_sent <= 1'b1;
        end
    end

    always @(posedge aclk) begin
        if (!aresetn) begin
            act_x_r          <= ZERO;
            act_y_r          <= ZERO;
            act_w_r          <= ZERO;
            act_h_r          <= ZERO;
            act_y_end_r      <= ONES;
            act_start_beat_r <= ZERO;
            act_nbeats_m1_r  <= ONES;
            act_sel_r        <= PPC[SEL_W-1:0];
        end else if (s_beat & sof) begin
            act_x_r          <= cfg_x;
            act_y_r          <= cfg_y;
            act_w_r          <= cfg_w;
            act_h_r          <= cfg_h;
            act_y_end_r      <= cfg_y_end;
            act_start_beat_r <= cfg_start_beat;
            act_nbeats_m1_r  <= cfg_nbeats_m1;
            act_sel_r        <= cfg_sel;
        end
    end

    always @(posedge aclk) begin
        if (!aresetn) begin
            geom_w_line <= ZERO;
            geom_w_r    <= ZERO;
            geom_h_r    <= ZERO;
            frame_cnt_r <= 32'd0;
        end else begin
            if (s_beat & s_tlast)
                geom_w_line <= (cur_beat + ONE) << LOG2PPC;

            if (s_beat & sof) begin
                geom_w_r    <= geom_w_line;
                geom_h_r    <= line_cnt;
                frame_cnt_r <= frame_cnt_r + 32'd1;
            end
        end
    end

    assign sts_short_line = s_beat & s_tlast & ~cfg_bypass & eff_armed
                          & v_active & ~cur_done
                          & (eff_nbeats != ONES)
                          & ~(emit_beat & last_out);

    assign sts_window_oob = s_beat & sof & ~cfg_bypass
                          & (act_y_end_r != ONES)
                          & (line_cnt < act_y_end_r);

    assign sts_sof_resync = s_beat & sof & (beat_idx != ZERO);

    assign act_x     = act_x_r;
    assign act_y     = act_y_r;
    assign act_w     = act_w_r;
    assign act_h     = act_h_r;
    assign geom_w    = geom_w_r;
    assign geom_h    = geom_h_r;
    assign frame_cnt = frame_cnt_r;

endmodule
