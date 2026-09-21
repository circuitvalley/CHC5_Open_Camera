/*
 * avs_axil_regs.v - AXI4-Lite register file for axis_video_subsample
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module avs_axil_regs #(
    parameter integer PPC = 2
)(
    input  wire        aclk,
    input  wire        aresetn,

    input  wire [5:0]  s_axi_awaddr,
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
    input  wire [5:0]  s_axi_araddr,
    input  wire [2:0]  s_axi_arprot,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output wire        s_axi_rvalid,
    input  wire        s_axi_rready,

    output wire [12:0] reg_left,
    output wire [12:0] reg_top,
    output wire [7:0]  reg_frame_num,

    output wire [12:0] h_right,
    output wire [12:0] v_bottom,
    output wire [12:0] h_last_pix,
    output wire [7:0]  eff_frame_den_m1
);

    reg        aw_en;
    reg        awready_r;
    reg        wready_r;
    reg        bvalid_r;
    reg        arready_r;
    reg        rvalid_r;
    reg [31:0] rdata_r;

    reg [12:0] h_in_r;
    reg [12:0] v_in_r;
    reg [12:0] h_out_r;
    reg [12:0] v_out_r;
    reg [12:0] left_r;
    reg [12:0] top_r;
    reg  [7:0] frame_num_r;
    reg  [7:0] frame_den_r;

    reg [12:0] h_right_r;
    reg [12:0] v_bottom_r;
    reg [12:0] h_last_pix_r;
    reg  [7:0] eff_frame_den_m1_r;

    localparam [31:0] ID_VAL     = 32'h4156_0100;
    localparam integer DIM_WIDTH  = 13;
    localparam integer RATE_WIDTH = 8;
    localparam [31:0] CONFIG_VAL = {12'd0, RATE_WIDTH[7:0], DIM_WIDTH[7:0], PPC[3:0]};

    reg [31:0] scratch_r;

    wire [3:0] wr_addr = s_axi_awaddr[5:2];
    wire       wr_hit  = awready_r && wready_r;

    always @(posedge aclk) begin
        if (!aresetn) begin
            scratch_r     <= 32'd0;
            awready_r     <= 1'b0;
            wready_r      <= 1'b0;
            bvalid_r      <= 1'b0;
            aw_en         <= 1'b1;
            h_in_r        <= 13'd0;
            v_in_r        <= 13'd0;
            h_out_r       <= 13'd0;
            v_out_r       <= 13'd0;
            left_r        <= 13'd0;
            top_r         <= 13'd0;
            frame_num_r   <= 8'd1;
            frame_den_r   <= 8'd1;
        end else begin
            if (~awready_r && s_axi_awvalid && s_axi_wvalid && aw_en) begin
                awready_r <= 1'b1;
                aw_en     <= 1'b0;
            end else begin
                awready_r <= 1'b0;
            end

            if (~wready_r && s_axi_wvalid && s_axi_awvalid && aw_en) begin
                wready_r <= 1'b1;
            end else begin
                wready_r <= 1'b0;
            end

            if (wr_hit) begin
                case (wr_addr)
                    4'd0:  h_in_r      <= s_axi_wdata[12:0];
                    4'd1:  v_in_r      <= s_axi_wdata[12:0];
                    4'd2:  h_out_r     <= s_axi_wdata[12:0];
                    4'd3:  v_out_r     <= s_axi_wdata[12:0];
                    4'd4:  left_r      <= s_axi_wdata[12:0];
                    4'd5:  top_r       <= s_axi_wdata[12:0];
                    4'd6:  frame_num_r <= s_axi_wdata[7:0];
                    4'd7:  frame_den_r <= s_axi_wdata[7:0];
                    4'd10: scratch_r   <= s_axi_wdata;
                    default: ;
                endcase
            end

            if (wr_hit && ~bvalid_r) begin
                bvalid_r <= 1'b1;
            end else if (bvalid_r && s_axi_bready) begin
                bvalid_r <= 1'b0;
                aw_en    <= 1'b1;
            end
        end
    end

    always @(posedge aclk) begin
        if (!aresetn) begin
            arready_r <= 1'b0;
            rvalid_r  <= 1'b0;
            rdata_r   <= 32'd0;
        end else begin
            if (~arready_r && s_axi_arvalid && ~rvalid_r) begin
                arready_r <= 1'b1;
                rdata_r   <= 32'd0;
                case (s_axi_araddr[5:2])
                    4'd0:  rdata_r[12:0] <= h_in_r;
                    4'd1:  rdata_r[12:0] <= v_in_r;
                    4'd2:  rdata_r[12:0] <= h_out_r;
                    4'd3:  rdata_r[12:0] <= v_out_r;
                    4'd4:  rdata_r[12:0] <= left_r;
                    4'd5:  rdata_r[12:0] <= top_r;
                    4'd6:  rdata_r[7:0]  <= frame_num_r;
                    4'd7:  rdata_r[7:0]  <= frame_den_r;
                    4'd8:  rdata_r       <= ID_VAL;
                    4'd9:  rdata_r       <= CONFIG_VAL;
                    4'd10: rdata_r       <= scratch_r;
                    default: ;
                endcase
            end else begin
                arready_r <= 1'b0;
            end

            if (arready_r && ~rvalid_r) begin
                rvalid_r <= 1'b1;
            end else if (rvalid_r && s_axi_rready) begin
                rvalid_r <= 1'b0;
            end
        end
    end

    reg wr_pending;

    always @(posedge aclk) begin
        if (!aresetn)
            wr_pending <= 1'b0;
        else
            wr_pending <= wr_hit;
    end

    always @(posedge aclk) begin
        if (!aresetn) begin
            h_right_r          <= 13'd0;
            v_bottom_r         <= 13'd0;
            h_last_pix_r       <= 13'd0;
            eff_frame_den_m1_r <= 8'd0;
        end else if (wr_pending) begin
            h_right_r          <= left_r + h_out_r;
            v_bottom_r         <= top_r  + v_out_r;
            h_last_pix_r       <= left_r + h_out_r - PPC[12:0];
            eff_frame_den_m1_r <= (frame_den_r == 8'd0) ? 8'd0 : (frame_den_r - 8'd1);
        end
    end

    assign s_axi_awready = awready_r;
    assign s_axi_wready  = wready_r;
    assign s_axi_bresp   = 2'b00;
    assign s_axi_bvalid  = bvalid_r;
    assign s_axi_arready = arready_r;
    assign s_axi_rdata   = rdata_r;
    assign s_axi_rresp   = 2'b00;
    assign s_axi_rvalid  = rvalid_r;

    assign reg_left      = left_r;
    assign reg_top       = top_r;
    assign reg_frame_num = frame_num_r;

    assign h_right          = h_right_r;
    assign v_bottom         = v_bottom_r;
    assign h_last_pix       = h_last_pix_r;
    assign eff_frame_den_m1 = eff_frame_den_m1_r;

endmodule
