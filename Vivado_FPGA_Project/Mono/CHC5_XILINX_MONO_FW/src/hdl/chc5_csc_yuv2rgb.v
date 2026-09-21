/*
 * chc5_csc_yuv2rgb.v - YUV422 (BT.709 limited range) to RGB888 colour-space converter
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module chc5_csc_yuv2rgb #(
    parameter integer PPC       = 4,
    parameter integer CW        = 8,
    parameter integer ROUND_HALF_UP = 1
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXIS:M_AXIS, ASSOCIATED_RESET aresetn" *)
    input  wire                        aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire                        aresetn,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TDATA" *)
    (* X_INTERFACE_PARAMETER = "HAS_TKEEP 1, HAS_TSTRB 0, HAS_TREADY 1, HAS_TLAST 1, TUSER_WIDTH 1" *)
    input  wire [PPC*2*CW-1:0]         s_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TKEEP" *)
    input  wire [PPC*2*CW/8-1:0]       s_axis_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TVALID" *)
    input  wire                        s_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TREADY" *)
    output wire                        s_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TLAST" *)
    input  wire                        s_axis_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TUSER" *)
    input  wire [0:0]                  s_axis_tuser,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TDATA" *)
    (* X_INTERFACE_PARAMETER = "HAS_TKEEP 1, HAS_TSTRB 0, HAS_TREADY 1, HAS_TLAST 1, TUSER_WIDTH 1" *)
    output wire [PPC*3*CW-1:0]         m_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TKEEP" *)
    output wire [PPC*3*CW/8-1:0]       m_axis_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TVALID" *)
    output wire                        m_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TREADY" *)
    input  wire                        m_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TLAST" *)
    output wire                        m_axis_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TUSER" *)
    output wire [0:0]                  m_axis_tuser
);
    localparam integer FRAC = 12;
    localparam integer ACC_W = 24;
    localparam signed [ACC_W-1:0] ROUND = ROUND_HALF_UP ? (1 <<< (FRAC-1)) : 0;

    localparam signed [15:0] C_Y   =  4769;
    localparam signed [15:0] C_RCr =  7342;
    localparam signed [15:0] C_GCb =  -873;
    localparam signed [15:0] C_GCr = -2182;
    localparam signed [15:0] C_BCb =  8652;

    localparam signed [ACC_W-1:0] OFF_Y = 16, OFF_C = 128;
    localparam [CW-1:0] CLAMP_MIN = {CW{1'b0}};
    localparam [CW-1:0] CLIP_MAX  = {CW{1'b1}};

    wire ce = m_axis_tready;
    assign s_axis_tready = aresetn & m_axis_tready;

    function [CW-1:0] sh_clamp(input signed [ACC_W-1:0] acc);
        reg signed [ACC_W-1:0] v;
        begin
            v = acc >>> FRAC;
            if      (v < $signed({{(ACC_W-CW){1'b0}}, CLAMP_MIN})) sh_clamp = CLAMP_MIN;
            else if (v > $signed({{(ACC_W-CW){1'b0}}, CLIP_MAX }))  sh_clamp = CLIP_MAX;
            else                                                    sh_clamp = v[CW-1:0];
        end
    endfunction

    assign m_axis_tkeep = {(PPC*3*CW/8){1'b1}};

    genvar L;
    generate
    if (PPC == 1) begin : gen_ppc1
        wire [CW-1:0] Y_in     = s_axis_tdata[0*CW +: CW];
        wire [CW-1:0] chroma_in= s_axis_tdata[1*CW +: CW];

        reg [CW-1:0] Yh, Cbh, Crh;
        reg          phase, primed, armed;
        reg          uh, lh;

        reg [CW-1:0] g_Y, g_Cb, g_Cr;
        reg          g_v, g_u, g_l;

        wire is_even = s_axis_tuser[0] | (phase == 1'b0);

        always @(posedge aclk) begin
            if (!aresetn) begin
                phase <= 1'b0; primed <= 1'b0; armed <= 1'b0;
                g_v   <= 1'b0;
            end else if (ce) begin
                if (s_axis_tvalid) begin
                    g_Y  <= Yh;
                    g_Cb <= Cbh;
                    g_Cr <= is_even ? Crh : chroma_in;
                    g_v  <= primed & armed;
                    g_u  <= uh;
                    g_l  <= lh;
                    Yh    <= Y_in;
                    if (is_even) Cbh <= chroma_in; else Crh <= chroma_in;
                    phase <= is_even ? 1'b1 : 1'b0;
                    uh    <= s_axis_tuser[0];
                    lh    <= s_axis_tlast;
                    primed<= 1'b1;
                    if (s_axis_tuser[0]) armed <= 1'b1;
                end else if (primed && (phase == 1'b0)) begin
                    g_Y  <= Yh; g_Cb <= Cbh; g_Cr <= Crh;
                    g_v  <= armed; g_u <= uh; g_l <= lh;
                    primed <= 1'b0;
                end else begin
                    g_v  <= 1'b0;
                end
            end
        end

        wire signed [ACC_W-1:0] ybp = $signed({1'b0, g_Y}) - OFF_Y;
        wire signed [ACC_W-1:0] cbp = $signed({1'b0, g_Cb}) - OFF_C;
        wire signed [ACC_W-1:0] crp = $signed({1'b0, g_Cr}) - OFF_C;

        reg signed [ACC_W-1:0] pL, pRc, pGb, pGr, pBb;
        reg signed [ACC_W-1:0] aR, aG, aB;
        reg [CW-1:0]           Rr, Br, Gr;
        reg                    v1, v2, v3, u1, u2, u3, l1, l2, l3;
        always @(posedge aclk) begin
            if (!aresetn) begin v1<=1'b0; v2<=1'b0; v3<=1'b0; end
            else if (ce) begin
                pL <= C_Y*ybp; pRc <= C_RCr*crp; pGb <= C_GCb*cbp; pGr <= C_GCr*crp; pBb <= C_BCb*cbp;
                v1 <= g_v; u1 <= g_u; l1 <= g_l;
                aR <= pL + pRc       + ROUND;  aG <= pL + pGb + pGr + ROUND;  aB <= pL + pBb + ROUND;
                v2 <= v1; u2 <= u1; l2 <= l1;
                Rr <= sh_clamp(aR); Gr <= sh_clamp(aG); Br <= sh_clamp(aB);
                v3 <= v2; u3 <= u2; l3 <= l2;
            end
        end

        assign m_axis_tdata  = {Rr, Br, Gr};
        assign m_axis_tvalid = v3;
        assign m_axis_tlast  = l3;
        assign m_axis_tuser  = u3;

    end else begin : gen_even
        reg  armed;
        wire beat_go = s_axis_tvalid & (armed | s_axis_tuser[0]);
        always @(posedge aclk)
            if (!aresetn)                                   armed <= 1'b0;
            else if (ce & s_axis_tvalid & s_axis_tuser[0])  armed <= 1'b1;

        reg [PPC*2*CW-1:0] d0;
        reg [3:0]          vldp, lastp, userp;
        always @(posedge aclk) begin
            if (!aresetn) begin
                vldp <= 4'b0; lastp <= 4'b0; userp <= 4'b0;
            end else if (ce) begin
                d0    <= s_axis_tdata;
                vldp  <= {vldp[2:0],  beat_go};
                lastp <= {lastp[2:0], s_axis_tlast};
                userp <= {userp[2:0], s_axis_tuser[0]};
            end
        end

        for (L = 0; L < PPC; L = L + 2) begin : gen_pair
            wire [CW-1:0] Y0 = d0[ L   *2*CW + 0*CW +: CW];
            wire [CW-1:0] Y1 = d0[(L+1)*2*CW + 0*CW +: CW];
            wire [CW-1:0] Cb = d0[ L   *2*CW + 1*CW +: CW];
            wire [CW-1:0] Cr = d0[(L+1)*2*CW + 1*CW +: CW];
            wire signed [ACC_W-1:0] yb0 = $signed({1'b0, Y0}) - OFF_Y;
            wire signed [ACC_W-1:0] yb1 = $signed({1'b0, Y1}) - OFF_Y;
            wire signed [ACC_W-1:0] cbs = $signed({1'b0, Cb}) - OFF_C;
            wire signed [ACC_W-1:0] crs = $signed({1'b0, Cr}) - OFF_C;

            reg signed [ACC_W-1:0] pRc, pGb, pGr, pBb;
            reg signed [ACC_W-1:0] pL0, pL1;
            reg signed [ACC_W-1:0] aR0, aG0, aB0, aR1, aG1, aB1;
            reg [CW-1:0] R0, B0, G0, R1, B1, G1;
            always @(posedge aclk) if (ce) begin
                pRc <= C_RCr*crs; pGb <= C_GCb*cbs; pGr <= C_GCr*crs; pBb <= C_BCb*cbs;
                pL0 <= C_Y*yb0;   pL1 <= C_Y*yb1;

                aR0 <= pL0 + pRc       + ROUND;  aG0 <= pL0 + pGb + pGr + ROUND;  aB0 <= pL0 + pBb + ROUND;
                aR1 <= pL1 + pRc       + ROUND;  aG1 <= pL1 + pGb + pGr + ROUND;  aB1 <= pL1 + pBb + ROUND;

                R0 <= sh_clamp(aR0); G0 <= sh_clamp(aG0); B0 <= sh_clamp(aB0);
                R1 <= sh_clamp(aR1); G1 <= sh_clamp(aG1); B1 <= sh_clamp(aB1);
            end

            assign m_axis_tdata[ L   *3*CW +: 3*CW] = {R0, B0, G0};
            assign m_axis_tdata[(L+1)*3*CW +: 3*CW] = {R1, B1, G1};
        end

        assign m_axis_tvalid = vldp[3];
        assign m_axis_tlast  = lastp[3];
        assign m_axis_tuser  = userp[3];
    end
    endgenerate

endmodule
