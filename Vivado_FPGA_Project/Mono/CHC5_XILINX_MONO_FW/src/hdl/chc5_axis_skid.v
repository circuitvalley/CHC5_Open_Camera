/*
 * chc5_axis_skid.v - 2-deep AXI4-Stream skid buffer with registered tready
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module chc5_axis_skid #(
    parameter integer DW = 96,
    parameter integer KW = 12
)(
    input  wire          aclk,
    input  wire          aresetn,

    input  wire [DW-1:0] s_tdata,
    input  wire [KW-1:0] s_tkeep,
    input  wire          s_tvalid,
    output wire          s_tready,
    input  wire          s_tlast,
    input  wire [0:0]    s_tuser,

    output wire [DW-1:0] m_tdata,
    output wire [KW-1:0] m_tkeep,
    output wire          m_tvalid,
    input  wire          m_tready,
    output wire          m_tlast,
    output wire [0:0]    m_tuser
);
    localparam integer W = DW + KW + 2;

    reg  [W-1:0] buf0, buf1;
    reg          v0, v1;
    reg          rdy_r;

    wire [W-1:0] s_pack = {s_tdata, s_tkeep, s_tlast, s_tuser[0]};
    wire         s_fire = s_tvalid & rdy_r;
    wire         m_fire = v0 & m_tready;

    wire [1:0] occ_nxt = ({1'b0,v0} + {1'b0,v1}) + {1'b0,s_fire} - {1'b0,m_fire};

    always @(posedge aclk) begin
        if (!aresetn) begin
            v0 <= 1'b0; v1 <= 1'b0; rdy_r <= 1'b0;
        end else begin
            if (m_fire & v1)                     buf0 <= buf1;
            if (s_fire) begin
                if (v0 & (~m_fire | v1))         buf1 <= s_pack;
                else                             buf0 <= s_pack;
            end
            case ({s_fire, m_fire})
                2'b10:   begin if (v0) v1 <= 1'b1; else v0 <= 1'b1; end
                2'b01:   begin if (v1) v1 <= 1'b0; else v0 <= 1'b0; end
                default: ;
            endcase
            rdy_r <= (occ_nxt <= 2'd1);
        end
    end

    assign s_tready = rdy_r;
    assign m_tvalid = v0;
    assign m_tdata  = buf0[(KW+2) +: DW];
    assign m_tkeep  = buf0[2 +: KW];
    assign m_tlast  = buf0[1];
    assign m_tuser  = buf0[0];

endmodule
