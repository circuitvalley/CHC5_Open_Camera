/*
 * pixel_packer_mux.v - pixel packer format select mux, parameterized by PPC and BPP
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module pixel_packer_mux #(
    parameter PPC = 2,
    parameter BPP = 12
)(
    input  wire [2:0]            fmt,
    input  wire [PPC*BPP-1:0]   s_axis_bayer_tdata,
    input  wire                  s_axis_bayer_tvalid,
    output wire                  s_axis_bayer_tready,
    input  wire                  s_axis_bayer_tlast,
    input  wire [0:0]            s_axis_bayer_tuser,
    input  wire [PPC*24-1:0]    s_axis_rgb_tdata,
    input  wire                  s_axis_rgb_tvalid,
    output wire                  s_axis_rgb_tready,
    input  wire                  s_axis_rgb_tlast,
    input  wire [0:0]            s_axis_rgb_tuser,
    input  wire [PPC*16-1:0]    s_axis_yuv_tdata,
    input  wire                  s_axis_yuv_tvalid,
    output wire                  s_axis_yuv_tready,
    input  wire                  s_axis_yuv_tlast,
    input  wire [0:0]            s_axis_yuv_tuser,
    output reg  [PPC*24-1:0]    sel_tdata,
    output reg                   sel_tvalid,
    input  wire                  sel_tready,
    output reg                   sel_tlast,
    output reg  [0:0]            sel_tuser
);
    localparam SW = PPC * 24;
    localparam IW = PPC * BPP;
    integer p;

    always @(*) begin
        sel_tdata = {SW{1'b0}}; sel_tvalid = 0; sel_tlast = 0; sel_tuser = 0;
        case (fmt)
            3'd0: begin
                for (p = 0; p < PPC; p = p + 1)
                    sel_tdata[p*8 +: 8] = s_axis_bayer_tdata[p*BPP+(BPP-8) +: 8];
                sel_tvalid = s_axis_bayer_tvalid; sel_tlast = s_axis_bayer_tlast; sel_tuser = s_axis_bayer_tuser;
            end
            3'd1: begin
                sel_tdata[IW-1:0] = s_axis_bayer_tdata;
                sel_tvalid = s_axis_bayer_tvalid; sel_tlast = s_axis_bayer_tlast; sel_tuser = s_axis_bayer_tuser;
            end
            3'd2: begin
                for (p = 0; p < PPC; p = p + 1)
                    sel_tdata[p*16 +: 16] = {{(16-BPP){1'b0}}, s_axis_bayer_tdata[p*BPP +: BPP]};
                sel_tvalid = s_axis_bayer_tvalid; sel_tlast = s_axis_bayer_tlast; sel_tuser = s_axis_bayer_tuser;
            end
            3'd3: begin
                for (p = 0; p < PPC; p = p + 1)
                    sel_tdata[p*16 +: 16] = {
                        s_axis_rgb_tdata[p*24+19 +: 5],
                        s_axis_rgb_tdata[p*24+2  +: 6],
                        s_axis_rgb_tdata[p*24+11 +: 5]};
                sel_tvalid = s_axis_rgb_tvalid; sel_tlast = s_axis_rgb_tlast; sel_tuser = s_axis_rgb_tuser;
            end
            3'd4: begin
                sel_tdata = s_axis_yuv_tdata;
                sel_tvalid = s_axis_yuv_tvalid; sel_tlast = s_axis_yuv_tlast; sel_tuser = s_axis_yuv_tuser;
            end
            3'd5: begin
                for (p = 0; p < PPC; p = p + 1)
                    sel_tdata[p*8 +: 8] = s_axis_yuv_tdata[p*16 +: 8];
                sel_tvalid = s_axis_yuv_tvalid; sel_tlast = s_axis_yuv_tlast; sel_tuser = s_axis_yuv_tuser;
            end
            3'd6, 3'd7: begin
                for (p = 0; p < PPC; p = p + 1)
                    sel_tdata[p*24 +: 24] = {s_axis_rgb_tdata[p*24+16 +: 8],
                                             s_axis_rgb_tdata[p*24    +: 8],
                                             s_axis_rgb_tdata[p*24+8  +: 8]};
                sel_tvalid = s_axis_rgb_tvalid; sel_tlast = s_axis_rgb_tlast; sel_tuser = s_axis_rgb_tuser;
            end
            default: ;
        endcase
    end

    wire bsel = (fmt==0)||(fmt==1)||(fmt==2);
    wire rsel = (fmt==3)||(fmt==6)||(fmt==7);
    wire ysel = (fmt==4)||(fmt==5);
    assign s_axis_bayer_tready = bsel ? sel_tready : 1'b1;
    assign s_axis_rgb_tready   = rsel ? sel_tready : 1'b1;
    assign s_axis_yuv_tready   = ysel ? sel_tready : 1'b1;
endmodule
