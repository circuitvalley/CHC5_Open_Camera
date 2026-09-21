/*
 * pixel_packer_core.v - pixel packer accumulator, parameterized by PPC and BPP
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module pixel_packer_core #(
    parameter PPC = 2,
    parameter BPP = 12
)(
    input  wire        aclk,
    input  wire        aresetn,
    input  wire [2:0]  pack_mode,
    input  wire        fmt_changed,

    input  wire [PPC*24-1:0] sel_tdata,
    input  wire              sel_tvalid,
    output wire              sel_tready,
    input  wire              sel_tlast,
    input  wire [0:0]        sel_tuser,

    output wire [PPC*16-1:0] m_axis_tdata,
    output wire              m_axis_tvalid,
    input  wire              m_axis_tready,
    output wire              m_axis_tlast,
    output wire [0:0]        m_axis_tuser,
    output wire [PPC*2-1:0]  m_axis_tkeep
);

    localparam [2:0] PM_THRU = 3'd0, PM_HALF = 3'd1, PM_RAW = 3'd2,
                     PM_XRGB = 3'd3, PM_RGB24 = 3'd4;

    localparam OW   = PPC * 16;
    localparam IW   = PPC * BPP;
    localparam HW   = PPC * 8;
    localparam KW   = PPC * 2;
    localparam STEP = OW - IW;
    localparam NSTATES = IW / STEP;

    localparam SW   = PPC * 24;
    localparam RES1 = PPC * 8;
    localparam [KW-1:0] KRES = (1 << PPC) - 1;
    localparam [1:0] RS_E0 = 2'd0, RS_E1 = 2'd1, RS_E2 = 2'd2, RS_EFL = 2'd3;

    localparam [KW-1:0] KALL = (1 << KW) - 1;

    localparam [6:0] SA = IW;
    localparam [6:0] SB = IW - STEP;
    localparam [6:0] SC = IW - 2*STEP;
    localparam [6:0] SD = (NSTATES >= 4) ? (IW - 3*STEP) : 7'd127;
    localparam [6:0] SE = (NSTATES >= 5) ? (IW - 4*STEP) : 7'd126;
    localparam [6:0] SF = (NSTATES >= 6) ? (IW - 5*STEP) : 7'd125;
    localparam [6:0] SG = (NSTATES >= 7) ? (IW - 6*STEP) : 7'd124;

    localparam VA = IW;
    localparam VB = IW - STEP;
    localparam VC = IW - 2*STEP;
    localparam VD = (NSTATES >= 4) ? (IW - 3*STEP) : 1;
    localparam VE = (NSTATES >= 5) ? (IW - 4*STEP) : 1;
    localparam VF = (NSTATES >= 6) ? (IW - 5*STEP) : 1;
    localparam VG = (NSTATES >= 7) ? (IW - 6*STEP) : 1;

    localparam WA = OW - VA;
    localparam WB = OW - VB;
    localparam WC = OW - VC;
    localparam WD = (NSTATES >= 4) ? (OW - VD) : 1;
    localparam WE = (NSTATES >= 5) ? (OW - VE) : 1;
    localparam WF = (NSTATES >= 6) ? (OW - VF) : 1;
    localparam WG = (NSTATES >= 7) ? (OW - VG) : 1;

    localparam RA = VA - STEP;
    localparam RB = VB - STEP;
    localparam RC = (NSTATES >= 4) ? (VC - STEP) : 1;
    localparam RD = (NSTATES >= 5) ? (VD - STEP) : 1;
    localparam RE = (NSTATES >= 6) ? (VE - STEP) : 1;
    localparam RF = (NSTATES >= 7) ? (VF - STEP) : 1;

    localparam [KW-1:0] KHW = (1 << (HW/8)) - 1;

    reg [IW-1:0]    acc;
    reg [6:0]       acc_cnt;
    reg             flushing;
    reg             sof_pending;

    reg             xph;
    reg [1:0]       rst8;
    reg [OW-1:0]    xacc;
    reg             xlast;

    reg [OW-1:0]    o_tdata;
    reg             o_tvalid, o_tlast, o_tuser_r;
    reg [KW-1:0]    o_tkeep;
    reg             in_ready;
    integer q;

    wire in_hs  = sel_tvalid & sel_tready;
    wire out_hs = o_tvalid & m_axis_tready;

    always @(*) begin
        o_tdata   = {OW{1'b0}};
        o_tvalid  = 1'b0;
        o_tlast   = 1'b0;
        o_tuser_r = 1'b0;
        o_tkeep   = KALL;
        in_ready  = 1'b0;

        if (fmt_changed) begin
            o_tvalid = 1'b0;
            in_ready = 1'b0;

        end else if (flushing) begin
            o_tvalid = (acc_cnt != 0);
            o_tlast  = 1'b1;
            o_tuser_r = sof_pending;
            in_ready = 1'b0;
            case (acc_cnt)
                HW[6:0]: begin o_tdata[0 +: HW] = acc[0 +: HW]; o_tkeep = KHW; end
                SA:      begin o_tdata[0 +: VA] = acc[0 +: VA]; o_tkeep = (1 << ((VA+7)/8)) - 1; end
                SB:      begin o_tdata[0 +: VB] = acc[0 +: VB]; o_tkeep = (1 << ((VB+7)/8)) - 1; end
                SC:      begin o_tdata[0 +: VC] = acc[0 +: VC]; o_tkeep = (1 << ((VC+7)/8)) - 1; end
                SD:      begin o_tdata[0 +: VD] = acc[0 +: VD]; o_tkeep = (1 << ((VD+7)/8)) - 1; end
                SE:      begin o_tdata[0 +: VE] = acc[0 +: VE]; o_tkeep = (1 << ((VE+7)/8)) - 1; end
                SF:      begin o_tdata[0 +: VF] = acc[0 +: VF]; o_tkeep = (1 << ((VF+7)/8)) - 1; end
                SG:      begin o_tdata[0 +: VG] = acc[0 +: VG]; o_tkeep = (1 << ((VG+7)/8)) - 1; end
                default: o_tvalid = 1'b0;
            endcase

        end else if (pack_mode == PM_THRU) begin
            o_tdata   = sel_tdata[OW-1:0];
            o_tvalid  = sel_tvalid;
            o_tlast   = sel_tlast;
            o_tuser_r = sel_tuser | sof_pending;
            o_tkeep   = KALL;
            in_ready  = m_axis_tready;

        end else if (pack_mode == PM_XRGB) begin
            o_tvalid = sel_tvalid;
            o_tkeep  = KALL;
            for (q = 0; q < PPC/2; q = q + 1)
                o_tdata[q*32 +: 32] = xph
                    ? {8'hFF, sel_tdata[(q+PPC/2)*24 +: 24]}
                    : {8'hFF, sel_tdata[q*24 +: 24]};
            if (!xph) begin
                o_tlast   = 1'b0;
                o_tuser_r = sel_tuser | sof_pending;
                in_ready  = 1'b0;
            end else begin
                o_tlast   = sel_tlast;
                o_tuser_r = 1'b0;
                in_ready  = m_axis_tready;
            end

        end else if (pack_mode == PM_RGB24) begin
            case (rst8)
                RS_E0: begin
                    o_tdata   = sel_tdata[OW-1:0];
                    o_tvalid  = sel_tvalid;
                    o_tlast   = 1'b0;
                    o_tuser_r = sel_tuser | sof_pending;
                    o_tkeep   = KALL;
                    in_ready  = m_axis_tready;
                end
                RS_E1: begin
                    o_tdata   = {sel_tdata[RES1-1:0], xacc[RES1-1:0]};
                    o_tvalid  = sel_tvalid;
                    o_tlast   = 1'b0;
                    o_tuser_r = 1'b0;
                    o_tkeep   = KALL;
                    in_ready  = m_axis_tready;
                end
                RS_E2: begin
                    o_tdata   = xacc;
                    o_tvalid  = 1'b1;
                    o_tlast   = xlast;
                    o_tuser_r = 1'b0;
                    o_tkeep   = KALL;
                    in_ready  = 1'b0;
                end
                RS_EFL: begin
                    o_tdata   = {{(OW-RES1){1'b0}}, xacc[RES1-1:0]};
                    o_tvalid  = 1'b1;
                    o_tlast   = 1'b1;
                    o_tuser_r = 1'b0;
                    o_tkeep   = KRES;
                    in_ready  = 1'b0;
                end
            endcase

        end else if (pack_mode == PM_HALF) begin
            case (acc_cnt)
                7'd0:    begin in_ready = 1'b1; o_tvalid = 1'b0; end
                HW[6:0]: begin
                    in_ready  = m_axis_tready;
                    o_tdata   = {sel_tdata[0 +: HW], acc[0 +: HW]};
                    o_tvalid  = sel_tvalid;
                    o_tlast   = sel_tlast;
                    o_tuser_r = sof_pending;
                end
                default: in_ready = 1'b0;
            endcase

        end else begin
            case (acc_cnt)
                7'd0: begin in_ready = 1'b1; o_tvalid = 1'b0; end
                SA: begin
                    in_ready  = m_axis_tready;
                    o_tdata   = {sel_tdata[0 +: WA], acc[0 +: VA]};
                    o_tvalid  = sel_tvalid;
                    o_tlast   = 1'b0;
                    o_tuser_r = sof_pending;
                end
                SB: begin
                    in_ready  = m_axis_tready;
                    o_tdata   = {sel_tdata[0 +: WB], acc[0 +: VB]};
                    o_tvalid  = sel_tvalid;
                    o_tlast   = (NSTATES == 2) ? sel_tlast : 1'b0;
                    o_tuser_r = sof_pending;
                end
                SC: begin
                    in_ready  = m_axis_tready;
                    o_tdata   = {sel_tdata[0 +: WC], acc[0 +: VC]};
                    o_tvalid  = sel_tvalid;
                    o_tlast   = (NSTATES == 3) ? sel_tlast : 1'b0;
                    o_tuser_r = sof_pending;
                end
                SD: begin
                    in_ready  = m_axis_tready;
                    o_tdata   = {sel_tdata[0 +: WD], acc[0 +: VD]};
                    o_tvalid  = sel_tvalid;
                    o_tlast   = (NSTATES == 4) ? sel_tlast : 1'b0;
                    o_tuser_r = sof_pending;
                end
                SE: begin
                    in_ready  = m_axis_tready;
                    o_tdata   = {sel_tdata[0 +: WE], acc[0 +: VE]};
                    o_tvalid  = sel_tvalid;
                    o_tlast   = (NSTATES == 5) ? sel_tlast : 1'b0;
                    o_tuser_r = sof_pending;
                end
                SF: begin
                    in_ready  = m_axis_tready;
                    o_tdata   = {sel_tdata[0 +: WF], acc[0 +: VF]};
                    o_tvalid  = sel_tvalid;
                    o_tlast   = (NSTATES == 6) ? sel_tlast : 1'b0;
                    o_tuser_r = sof_pending;
                end
                SG: begin
                    in_ready  = m_axis_tready;
                    o_tdata   = {sel_tdata[0 +: WG], acc[0 +: VG]};
                    o_tvalid  = sel_tvalid;
                    o_tlast   = sel_tlast;
                    o_tuser_r = sof_pending;
                end
                default: in_ready = 1'b0;
            endcase
        end
    end

    assign sel_tready   = in_ready;
    assign m_axis_tdata = o_tdata;
    assign m_axis_tvalid = o_tvalid;
    assign m_axis_tlast = o_tlast;
    assign m_axis_tuser = o_tuser_r;
    assign m_axis_tkeep = o_tkeep;

    always @(posedge aclk) begin
        if (!aresetn || fmt_changed) begin
            acc <= {IW{1'b0}}; acc_cnt <= 0; flushing <= 0; sof_pending <= 0;
            xph <= 1'b0; rst8 <= RS_E0; xacc <= {OW{1'b0}}; xlast <= 1'b0;
        end else begin
            if (in_hs && sel_tuser[0] && !out_hs) sof_pending <= 1'b1;
            else if (out_hs) sof_pending <= 1'b0;

            if (flushing) begin
                if (out_hs) begin flushing <= 0; acc_cnt <= 0; acc <= {IW{1'b0}}; end
            end else if (pack_mode == PM_THRU) begin
            end else if (pack_mode == PM_XRGB) begin
                if (out_hs) xph <= ~xph;
            end else if (pack_mode == PM_RGB24) begin
                case (rst8)
                    RS_E0: if (in_hs) begin
                        xacc[RES1-1:0] <= sel_tdata[SW-1 -: RES1];
                        rst8 <= sel_tlast ? RS_EFL : RS_E1;
                    end
                    RS_E1: if (in_hs) begin
                        xacc  <= sel_tdata[SW-1 -: OW];
                        xlast <= sel_tlast;
                        rst8  <= RS_E2;
                    end
                    RS_E2: if (out_hs) begin
                        rst8 <= RS_E0; xlast <= 1'b0; xacc <= {OW{1'b0}};
                    end
                    RS_EFL: if (out_hs) begin
                        rst8 <= RS_E0; xacc <= {OW{1'b0}};
                    end
                endcase
            end else if (pack_mode == PM_HALF) begin
                if (in_hs) case (acc_cnt)
                    7'd0: begin
                        acc[0 +: HW] <= sel_tdata[0 +: HW];
                        acc_cnt <= HW[6:0];
                        if (sel_tlast) flushing <= 1;
                    end
                    HW[6:0]: begin acc_cnt <= 0; acc <= {IW{1'b0}}; end
                    default: ;
                endcase
            end else begin
                if (in_hs) case (acc_cnt)
                    7'd0: begin
                        acc[0 +: IW] <= sel_tdata[0 +: IW];
                        acc_cnt <= SA;
                        if (sel_tlast) flushing <= 1;
                    end
                    SA: begin
                        acc[0 +: RA] <= sel_tdata[WA +: RA];
                        acc_cnt <= SB;
                        if (sel_tlast) flushing <= 1;
                    end
                    SB: begin
                        if (NSTATES == 2) begin
                            acc_cnt <= 0; acc <= {IW{1'b0}};
                        end else begin
                            acc[0 +: RB] <= sel_tdata[WB +: RB];
                            acc_cnt <= SC;
                            if (sel_tlast) flushing <= 1;
                        end
                    end
                    SC: begin
                        if (NSTATES == 3) begin
                            acc_cnt <= 0; acc <= {IW{1'b0}};
                        end else begin
                            acc[0 +: RC] <= sel_tdata[WC +: RC];
                            acc_cnt <= SD;
                            if (sel_tlast) flushing <= 1;
                        end
                    end
                    SD: begin
                        if (NSTATES >= 4) begin
                            if (NSTATES == 4) begin
                                acc_cnt <= 0; acc <= {IW{1'b0}};
                            end else begin
                                acc[0 +: RD] <= sel_tdata[WD +: RD];
                                acc_cnt <= SE;
                                if (sel_tlast) flushing <= 1;
                            end
                        end
                    end
                    SE: begin
                        if (NSTATES >= 5) begin
                            if (NSTATES == 5) begin
                                acc_cnt <= 0; acc <= {IW{1'b0}};
                            end else begin
                                acc[0 +: RE] <= sel_tdata[WE +: RE];
                                acc_cnt <= SF;
                                if (sel_tlast) flushing <= 1;
                            end
                        end
                    end
                    SF: begin
                        if (NSTATES >= 6) begin
                            if (NSTATES == 6) begin
                                acc_cnt <= 0; acc <= {IW{1'b0}};
                            end else begin
                                acc[0 +: RF] <= sel_tdata[WF +: RF];
                                acc_cnt <= SG;
                                if (sel_tlast) flushing <= 1;
                            end
                        end
                    end
                    SG: begin
                        if (NSTATES >= 7) begin
                            acc_cnt <= 0; acc <= {IW{1'b0}};
                        end
                    end
                    default: ;
                endcase
            end
        end
    end
endmodule
