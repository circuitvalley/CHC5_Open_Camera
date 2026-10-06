/*
 * chc5_eof_fsync.v - end-of-frame frame sync for an AXI VDMA S2MM channel
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module chc5_eof_fsync #(
    parameter integer DATA_W   = 64,
    parameter integer PULSE_W  = 8,
    parameter integer ARM_IDLE = 16384
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi:s_axis:m_axis, ASSOCIATED_RESET aresetn" *)
    input  wire                  aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire                  aresetn,

    input  wire [DATA_W-1:0]     s_axis_tdata,
    input  wire [DATA_W/8-1:0]   s_axis_tkeep,
    input  wire                  s_axis_tvalid,
    output wire                  s_axis_tready,
    input  wire                  s_axis_tlast,
    input  wire [0:0]            s_axis_tuser,
    output wire [DATA_W-1:0]     m_axis_tdata,
    output wire [DATA_W/8-1:0]   m_axis_tkeep,
    output wire                  m_axis_tvalid,
    input  wire                  m_axis_tready,
    output wire                  m_axis_tlast,
    output wire [0:0]            m_axis_tuser,

    output reg                   fsync,

    (* X_INTERFACE_PARAMETER = "PROTOCOL AXI4LITE, ADDR_WIDTH 5, DATA_WIDTH 32" *)
    input  wire [4:0]            s_axi_awaddr,
    input  wire [2:0]            s_axi_awprot,
    input  wire                  s_axi_awvalid,
    output wire                  s_axi_awready,
    input  wire [31:0]           s_axi_wdata,
    input  wire [3:0]            s_axi_wstrb,
    input  wire                  s_axi_wvalid,
    output wire                  s_axi_wready,
    output wire [1:0]            s_axi_bresp,
    output wire                  s_axi_bvalid,
    input  wire                  s_axi_bready,
    input  wire [4:0]            s_axi_araddr,
    input  wire [2:0]            s_axi_arprot,
    input  wire                  s_axi_arvalid,
    output wire                  s_axi_arready,
    output wire [31:0]           s_axi_rdata,
    output wire [1:0]            s_axi_rresp,
    output wire                  s_axi_rvalid,
    input  wire                  s_axi_rready
);

    localparam [31:0] VERSION_VAL = 32'h454F_0100;

    assign m_axis_tdata  = s_axis_tdata;
    assign m_axis_tkeep  = s_axis_tkeep;
    assign m_axis_tvalid = s_axis_tvalid;
    assign m_axis_tlast  = s_axis_tlast;
    assign m_axis_tuser  = s_axis_tuser;
    assign s_axis_tready = m_axis_tready;

    reg        en_r;
    reg        arm_r;
    reg        arm_req;
    reg [15:0] lines_r;
    reg [15:0] delay_r;
    reg [15:0] syncs_r;
    reg [15:0] shorts_r;

    reg hs, sof, eol;
    always @(posedge aclk) begin
        hs  <= s_axis_tvalid & m_axis_tready;
        sof <= s_axis_tvalid & m_axis_tready & s_axis_tuser[0];
        eol <= s_axis_tvalid & m_axis_tready & s_axis_tlast;
    end

    reg        in_frame;
    reg        stray;
    reg [15:0] line_cnt;
    reg        pend;
    reg [15:0] dly_cnt;
    reg [$clog2(PULSE_W+1)-1:0] pw_cnt;
    reg [$clog2(ARM_IDLE+1)-1:0] idle_cnt;

    wire [15:0] cnt_base = sof ? 16'd0 : line_cnt;
    wire [15:0] cnt_next = cnt_base + (eol ? 16'd1 : 16'd0);
    wire        frame_on = sof | in_frame;
    wire        last_eol = en_r && frame_on && eol && lines_r != 0 && cnt_next == lines_r;
    wire        idle     = idle_cnt == ARM_IDLE[$clog2(ARM_IDLE+1)-1:0];
    wire        arm_now  = arm_req && !hs && idle && !in_frame && !stray && !pend;

    always @(posedge aclk) begin
        if (!aresetn || !en_r) begin
            in_frame <= 1'b0;
            stray    <= 1'b0;
            line_cnt <= 16'd0;
            pend     <= 1'b0;
            dly_cnt  <= 16'd0;
            pw_cnt   <= 0;
            fsync    <= 1'b0;
            arm_req  <= 1'b0;
            idle_cnt <= 0;
        end else begin
            if (hs)
                idle_cnt <= 0;
            else if (!idle)
                idle_cnt <= idle_cnt + 1'b1;
            if (arm_r)
                arm_req <= 1'b1;
            else if (hs || arm_now)
                arm_req <= 1'b0;

            if (last_eol) begin
                in_frame <= 1'b0;
                line_cnt <= 16'd0;
                stray    <= 1'b0;
            end else if (frame_on && hs) begin
                in_frame <= 1'b1;
                line_cnt <= cnt_next;
            end else if (hs && !frame_on) begin
                stray    <= 1'b1;
            end

            if (last_eol) begin
                pend    <= 1'b1;
                dly_cnt <= delay_r;
            end else if (arm_now) begin
                pend    <= 1'b1;
                dly_cnt <= 16'd0;
            end else if (pend && dly_cnt != 0) begin
                dly_cnt <= dly_cnt - 1'b1;
            end else if (pend) begin
                pend    <= 1'b0;
                pw_cnt  <= PULSE_W[$clog2(PULSE_W+1)-1:0];
            end

            if (pw_cnt != 0) begin
                fsync  <= 1'b1;
                pw_cnt <= pw_cnt - 1'b1;
            end else begin
                fsync  <= 1'b0;
            end
        end
    end

    always @(posedge aclk) begin
        if (!aresetn) begin
            syncs_r  <= 16'd0;
            shorts_r <= 16'd0;
        end else if (en_r) begin
            if (pend && dly_cnt == 0 && !last_eol && !arm_now)
                syncs_r <= syncs_r + 1'b1;
            if (sof && in_frame && line_cnt != 0)
                shorts_r <= shorts_r + 1'b1;
        end
    end

    reg        aw_ok, w_ok, bvalid_r;
    reg [4:0]  awaddr_r;
    reg [31:0] wdata_r;
    reg        rvalid_r;
    reg [31:0] rdata_r;

    assign s_axi_awready = !aw_ok && !bvalid_r;
    assign s_axi_wready  = !w_ok  && !bvalid_r;
    assign s_axi_bresp   = 2'b00;
    assign s_axi_bvalid  = bvalid_r;
    assign s_axi_arready = !rvalid_r;
    assign s_axi_rresp   = 2'b00;
    assign s_axi_rvalid  = rvalid_r;
    assign s_axi_rdata   = rdata_r;

    always @(posedge aclk) begin
        if (!aresetn) begin
            aw_ok    <= 1'b0;
            w_ok     <= 1'b0;
            bvalid_r <= 1'b0;
            awaddr_r <= 5'd0;
            wdata_r  <= 32'd0;
            en_r     <= 1'b0;
            arm_r    <= 1'b0;
            lines_r  <= 16'd0;
            delay_r  <= 16'd2048;
        end else begin
            arm_r <= 1'b0;
            if (s_axi_awvalid && s_axi_awready) begin
                aw_ok    <= 1'b1;
                awaddr_r <= s_axi_awaddr;
            end
            if (s_axi_wvalid && s_axi_wready) begin
                w_ok    <= 1'b1;
                wdata_r <= s_axi_wdata;
            end
            if (aw_ok && w_ok) begin
                case (awaddr_r[4:2])
                    3'd1: begin en_r <= wdata_r[0]; arm_r <= wdata_r[1]; end
                    3'd2: lines_r <= wdata_r[15:0];
                    3'd3: delay_r <= wdata_r[15:0];
                    default: ;
                endcase
                aw_ok    <= 1'b0;
                w_ok     <= 1'b0;
                bvalid_r <= 1'b1;
            end
            if (bvalid_r && s_axi_bready)
                bvalid_r <= 1'b0;
        end
    end

    always @(posedge aclk) begin
        if (!aresetn) begin
            rvalid_r <= 1'b0;
            rdata_r  <= 32'd0;
        end else begin
            if (s_axi_arvalid && s_axi_arready) begin
                rvalid_r <= 1'b1;
                case (s_axi_araddr[4:2])
                    3'd0: rdata_r <= VERSION_VAL;
                    3'd1: rdata_r <= {30'd0, 1'b0, en_r};
                    3'd2: rdata_r <= {16'd0, lines_r};
                    3'd3: rdata_r <= {16'd0, delay_r};
                    3'd4: rdata_r <= {shorts_r, syncs_r};
                    3'd5: rdata_r <= {line_cnt, 13'd0, pend, stray, in_frame};
                    default: rdata_r <= 32'd0;
                endcase
            end else if (rvalid_r && s_axi_rready) begin
                rvalid_r <= 1'b0;
            end
        end
    end

endmodule
