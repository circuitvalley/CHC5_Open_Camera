/*
 * crop_axil_regs.v - AXI4-Lite register file for chc5_axis_crop
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module crop_axil_regs #(
    parameter integer PPC             = 4,
    parameter integer COMPONENT_WIDTH = 12,
    parameter integer MAX_COLS        = 8192,
    parameter integer MAX_ROWS        = 8192,
    parameter integer DIM_W           = 16,
    parameter integer SEL_W           = (PPC <= 1) ? 1 : (PPC <= 2) ? 2 : (PPC <= 4) ? 3 : 4
)(
    input  wire        aclk,
    input  wire        aresetn,

    input  wire [11:0] s_axi_awaddr,
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
    input  wire [11:0] s_axi_araddr,
    input  wire [2:0]  s_axi_arprot,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output wire        s_axi_rvalid,
    input  wire        s_axi_rready,

    output wire                  cfg_enable,
    output wire                  cfg_bypass,
    output wire                  cfg_drop_until_sof,

    output wire [DIM_W-1:0]      cfg_x,
    output wire [DIM_W-1:0]      cfg_y,
    output wire [DIM_W-1:0]      cfg_w,
    output wire [DIM_W-1:0]      cfg_h,

    output wire [DIM_W-1:0]      cfg_y_end,
    output wire [DIM_W-1:0]      cfg_start_beat,
    output wire [DIM_W-1:0]      cfg_nbeats_m1,
    output wire [SEL_W-1:0]      cfg_sel,

    input  wire [DIM_W-1:0]      act_x,
    input  wire [DIM_W-1:0]      act_y,
    input  wire [DIM_W-1:0]      act_w,
    input  wire [DIM_W-1:0]      act_h,
    input  wire [DIM_W-1:0]      geom_w,
    input  wire [DIM_W-1:0]      geom_h,
    input  wire [31:0]           frame_cnt,
    input  wire                  sts_short_line,
    input  wire                  sts_window_oob,
    input  wire                  sts_sof_resync
);

    localparam integer LOG2PPC   = (PPC <= 1) ? 0 : (PPC <= 2) ? 1 :
                                   (PPC <= 4) ? 2 : 3;
    localparam [31:0]  PPC_MASK  = ~((32'd1 << LOG2PPC) - 32'd1);
    localparam [31:0]  MAXC_ALGN = MAX_COLS & PPC_MASK;
    localparam [31:0]  ID_VAL    = 32'hCC5C_0100;

    localparam [3:0] A_ID          = 4'd0;
    localparam [3:0] A_CTRL        = 4'd1;
    localparam [3:0] A_CROP_X      = 4'd2;
    localparam [3:0] A_CROP_Y      = 4'd3;
    localparam [3:0] A_CROP_W      = 4'd4;
    localparam [3:0] A_CROP_H      = 4'd5;
    localparam [3:0] A_ACTIVE_XY   = 4'd6;
    localparam [3:0] A_ACTIVE_WH   = 4'd7;
    localparam [3:0] A_FRAME_GEOM  = 4'd8;
    localparam [3:0] A_STATUS      = 4'd9;
    localparam [3:0] A_FRAME_CNT   = 4'd10;

    reg        aw_en;
    reg        awready_r;
    reg        wready_r;
    reg        bvalid_r;
    reg        arready_r;
    reg        rvalid_r;
    reg [31:0] rdata_r;

    reg              enable_r;
    reg              bypass_r;
    reg              drop_sof_r;
    reg  [DIM_W-1:0] x_r;
    reg  [DIM_W-1:0] y_r;
    reg  [DIM_W-1:0] w_r;
    reg  [DIM_W-1:0] h_r;

    reg  [DIM_W-1:0] y_end_r;
    reg  [DIM_W-1:0] start_beat_r;
    reg  [DIM_W-1:0] nbeats_m1_r;
    reg  [SEL_W-1:0] sel_r;

    reg  [3:0]       status_r;

    wire [3:0] wr_addr  = s_axi_awaddr[5:2];
    wire       in_map_w = (s_axi_awaddr[11:6] == 6'd0);
    wire       wr_ack   = awready_r && wready_r;
    wire       wr_hit   = wr_ack && in_map_w;

    wire [31:0] wd        = s_axi_wdata;
    wire [31:0] wd_masked = wd & PPC_MASK;

    wire [31:0] x_val = (wd >= MAX_COLS) ? (MAX_COLS - 32'd1) : wd;
    wire [31:0] y_val = (wd >= MAX_ROWS) ? (MAX_ROWS - 32'd1) : wd;
    wire [31:0] w_val = (wd_masked > MAXC_ALGN) ? MAXC_ALGN : wd_masked;
    wire [31:0] h_val = (wd > MAX_ROWS) ? MAX_ROWS : wd;

    wire w_trunc = wr_hit && (wr_addr == A_CROP_W) && (wd != w_val);

    always @(posedge aclk) begin
        if (!aresetn) begin
            awready_r  <= 1'b0;
            wready_r   <= 1'b0;
            bvalid_r   <= 1'b0;
            aw_en      <= 1'b1;
            enable_r   <= 1'b1;
            bypass_r   <= 1'b0;
            drop_sof_r <= 1'b1;
            x_r        <= {DIM_W{1'b0}};
            y_r        <= {DIM_W{1'b0}};
            w_r        <= {DIM_W{1'b0}};
            h_r        <= {DIM_W{1'b0}};
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
                    A_CTRL: begin
                        enable_r   <= wd[0];
                        bypass_r   <= wd[1];
                        drop_sof_r <= wd[2];
                    end
                    A_CROP_X: x_r <= x_val[DIM_W-1:0];
                    A_CROP_Y: y_r <= y_val[DIM_W-1:0];
                    A_CROP_W: w_r <= w_val[DIM_W-1:0];
                    A_CROP_H: h_r <= h_val[DIM_W-1:0];
                    default: ;
                endcase
            end

            if (wr_ack && ~bvalid_r) begin
                bvalid_r <= 1'b1;
            end else if (bvalid_r && s_axi_bready) begin
                bvalid_r <= 1'b0;
                aw_en    <= 1'b1;
            end
        end
    end

    wire [3:0] sts_set = { sts_sof_resync,
                           sts_window_oob,
                           w_trunc,
                           sts_short_line };
    wire [3:0] sts_clr = (wr_hit && (wr_addr == A_STATUS)) ? wd[3:0] : 4'd0;

    always @(posedge aclk) begin
        if (!aresetn) status_r <= 4'd0;
        else          status_r <= (status_r & ~sts_clr) | sts_set;
    end

    wire [3:0] rd_addr  = s_axi_araddr[5:2];
    wire       in_map_r = (s_axi_araddr[11:6] == 6'd0);

    always @(posedge aclk) begin
        if (!aresetn) begin
            arready_r <= 1'b0;
            rvalid_r  <= 1'b0;
            rdata_r   <= 32'd0;
        end else begin
            if (~arready_r && s_axi_arvalid && ~rvalid_r) begin
                arready_r <= 1'b1;
                rdata_r   <= 32'd0;
                if (in_map_r) begin
                    case (rd_addr)
                        A_ID:         rdata_r <= ID_VAL;
                        A_CTRL:       rdata_r <= {29'd0, drop_sof_r, bypass_r, enable_r};
                        A_CROP_X:     rdata_r <= {{(32-DIM_W){1'b0}}, x_r};
                        A_CROP_Y:     rdata_r <= {{(32-DIM_W){1'b0}}, y_r};
                        A_CROP_W:     rdata_r <= {{(32-DIM_W){1'b0}}, w_r};
                        A_CROP_H:     rdata_r <= {{(32-DIM_W){1'b0}}, h_r};
                        A_ACTIVE_XY:  rdata_r <= {act_y[15:0], act_x[15:0]};
                        A_ACTIVE_WH:  rdata_r <= {act_h[15:0], act_w[15:0]};
                        A_FRAME_GEOM: rdata_r <= {geom_h[15:0], geom_w[15:0]};
                        A_STATUS:     rdata_r <= {28'd0, status_r};
                        A_FRAME_CNT:  rdata_r <= frame_cnt;
                        default:      rdata_r <= 32'd0;
                    endcase
                end
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
        if (!aresetn) wr_pending <= 1'b0;
        else          wr_pending <= wr_hit;
    end

    wire [DIM_W-1:0] phase = x_r & (PPC[DIM_W-1:0] - {{(DIM_W-1){1'b0}}, 1'b1});

    always @(posedge aclk) begin
        if (!aresetn) begin
            y_end_r      <= {DIM_W{1'b1}};
            start_beat_r <= {DIM_W{1'b0}};
            nbeats_m1_r  <= {DIM_W{1'b1}};
            sel_r        <= PPC[SEL_W-1:0];
        end else if (wr_pending) begin
            y_end_r      <= (h_r == {DIM_W{1'b0}}) ? {DIM_W{1'b1}} : (y_r + h_r);
            start_beat_r <= (x_r >> LOG2PPC) +
                            ((phase != {DIM_W{1'b0}}) ? {{(DIM_W-1){1'b0}}, 1'b1}
                                                      : {DIM_W{1'b0}});
            nbeats_m1_r  <= (w_r == {DIM_W{1'b0}}) ? {DIM_W{1'b1}}
                                                   : ((w_r >> LOG2PPC) -
                                                      {{(DIM_W-1){1'b0}}, 1'b1});
            sel_r        <= (phase == {DIM_W{1'b0}}) ? PPC[SEL_W-1:0]
                                                     : phase[SEL_W-1:0];
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

    assign cfg_enable         = enable_r;
    assign cfg_bypass         = bypass_r;
    assign cfg_drop_until_sof = drop_sof_r;

    assign cfg_x          = x_r;
    assign cfg_y          = y_r;
    assign cfg_w          = w_r;
    assign cfg_h          = h_r;

    assign cfg_y_end      = y_end_r;
    assign cfg_start_beat = start_beat_r;
    assign cfg_nbeats_m1  = nbeats_m1_r;
    assign cfg_sel        = sel_r;

endmodule
