/*
 * pixel_packer_axilite.v - AXI4-Lite register file for pixel_packer
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module pixel_packer_axilite #(
    parameter integer PPC           = 2,
    parameter integer BPP           = 12,
    parameter [7:0]   FMT_SUPPORTED = 8'h3F
) (
    input  wire        aclk,
    input  wire        aresetn,

    input  wire [3:0]  s_axi_awaddr,
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
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output wire        s_axi_rvalid,
    input  wire        s_axi_rready,

    output wire [2:0]  fmt_reg,
    output wire        fmt_changed
);

    localparam [31:0] VERSION_VAL = 32'h5050_0100;
    localparam [31:0] CONFIG_VAL  = {12'd0, BPP[7:0], PPC[3:0], FMT_SUPPORTED};

    localparam [1:0] A_FMT     = 2'd0;
    localparam [1:0] A_VERSION = 2'd1;
    localparam [1:0] A_CONFIG  = 2'd2;
    localparam [1:0] A_SCRATCH = 2'd3;

    reg        aw_en;
    reg        awready_r;
    reg        wready_r;
    reg        bvalid_r;
    reg        arready_r;
    reg        rvalid_r;
    reg [31:0] rdata_r;
    reg  [2:0] fmt_reg_r;
    reg [31:0] scratch_r;

    always @(posedge aclk) begin
        if (!aresetn) begin
            awready_r <= 1'b0;
            wready_r  <= 1'b0;
            bvalid_r  <= 1'b0;
            aw_en     <= 1'b1;
            fmt_reg_r <= 3'd1;
            scratch_r <= 32'd0;
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
            if (awready_r && wready_r) begin
                case (s_axi_awaddr[3:2])
                    A_FMT:     fmt_reg_r <= s_axi_wdata[2:0];
                    A_SCRATCH: scratch_r <= s_axi_wdata;
                    default:   ;
                endcase
            end
            if (awready_r && wready_r && ~bvalid_r) begin
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
                case (s_axi_araddr[3:2])
                    A_FMT:     rdata_r <= {29'd0, fmt_reg_r};
                    A_VERSION: rdata_r <= VERSION_VAL;
                    A_CONFIG:  rdata_r <= CONFIG_VAL;
                    A_SCRATCH: rdata_r <= scratch_r;
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

    assign s_axi_awready = awready_r;
    assign s_axi_wready  = wready_r;
    assign s_axi_bresp   = 2'b00;
    assign s_axi_bvalid  = bvalid_r;
    assign s_axi_arready = arready_r;
    assign s_axi_rdata   = rdata_r;
    assign s_axi_rresp   = 2'b00;
    assign s_axi_rvalid  = rvalid_r;

    assign fmt_reg = fmt_reg_r;

    reg [2:0] fmt_reg_d;
    always @(posedge aclk) fmt_reg_d <= fmt_reg_r;
    assign fmt_changed = (fmt_reg_r != fmt_reg_d);

endmodule
