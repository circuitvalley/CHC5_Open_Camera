/*
 * camio_sync_axil_regs.v - AXI4-Lite register file for chc5_camio_sync
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 * SPDX-License-Identifier: CC-BY-NC-ND-4.0
 * https://creativecommons.org/licenses/by-nc-nd/4.0/
 */

`timescale 1 ns / 1 ps

module camio_sync_axil_regs #(
    parameter integer DELAY_W = 16,
    parameter integer TB_W    = 20,
    parameter integer DIV_W   = 8,
    parameter [31:0]  INC_FRAC = 32'hAAAA_AAAB
)(
    input  wire        aclk,
    input  wire        aresetn,

    input  wire [8:0]  s_axi_awaddr,
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
    input  wire [8:0]  s_axi_araddr,
    input  wire [2:0]  s_axi_arprot,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output wire        s_axi_rvalid,
    input  wire        s_axi_rready,

    output wire               o_enable,
    output wire               o_soft_reset,
    output wire [1:0]         o_in_func,
    output wire [1:0]         o_in_activation,
    output wire               o_in_invert,
    output wire [DELAY_W-1:0] o_in_delay,
    output wire [DIV_W-1:0]   o_in_divider,
    output wire               o_strobe_en,
    output wire               o_strobe_invert,
    output wire [1:0]         o_strobe_src,
    output wire [DELAY_W-1:0] o_strobe_delay,
    output wire [DELAY_W-1:0] o_strobe_dur,
    output wire [DELAY_W-1:0] o_strobe_minon,
    output wire [2:0]         o_out_src,
    output wire               o_out_invert,
    output wire               o_out_user_value,
    output wire [1:0]         o_sync_role,
    output wire               o_xvs_dir,
    output wire               o_xhs_dir,
    output wire               o_xvs_pol,
    output wire               o_xhs_pol,
    output wire               o_role_by,
    output wire [1:0]         o_sync_trig_route,
    output wire               o_xhs_en,
    output wire [2:0]         o_cap_src,
    output wire               o_cap_edge,

    output wire               o_tb_en,
    output wire               o_evt_en,
    output wire               o_sched_en,
    output wire               o_load_pulse,
    output wire [63:0]        o_load_val,
    output wire               o_commit_pulse,
    output wire [31:0]        o_rate_adj,
    output wire               o_evt_arm,
    output wire [63:0]        o_evt_next,
    output wire [31:0]        o_evt_period,
    output wire [7:0]         o_evt_width,
    output wire               o_evt_pol,
    output wire [63:0]        o_sched_next,
    output wire [63:0]        o_sched_period,
    output wire [31:0]        o_sched_period_frac,
    output wire [23:0]        o_line_period,
    output wire [23:0]        o_frame_lines,
    output wire               o_cap_en,
    output wire               o_cap_latch,
    output wire               o_cap_pop,

    input  wire [31:0]        i_status,
    input  wire [31:0]        i_in_status,
    input  wire [31:0]        i_sync_status,

    input  wire [63:0]        i_time,
    input  wire [31:0]        i_ptp_status,
    input  wire [31:0]        i_evt_count,
    input  wire [31:0]        i_cap_head_lo,
    input  wire [31:0]        i_cap_hold_hi,
    input  wire [31:0]        i_cap_hold_meta,
    input  wire [31:0]        i_cap_status,
    input  wire               i_commit_busy
);
    localparam [31:0] VERSION_VAL = 32'hCC5A_0200;

    localparam [6:0] A_VERSION    = 7'd0;
    localparam [6:0] A_CONTROL    = 7'd1;
    localparam [6:0] A_STATUS     = 7'd2;
    localparam [6:0] A_IN_CFG     = 7'd4;
    localparam [6:0] A_IN_DELAY   = 7'd6;
    localparam [6:0] A_IN_DIVIDER = 7'd7;
    localparam [6:0] A_IN_STATUS  = 7'd8;
    localparam [6:0] A_ST_CFG     = 7'd12;
    localparam [6:0] A_ST_DELAY   = 7'd13;
    localparam [6:0] A_ST_DUR     = 7'd15;
    localparam [6:0] A_ST_MINON   = 7'd16;
    localparam [6:0] A_OUT_CFG    = 7'd20;
    localparam [6:0] A_SYNC_CFG   = 7'd24;
    localparam [6:0] A_SYNC_TROUTE= 7'd26;
    localparam [6:0] A_SYNC_STATUS= 7'd27;
    localparam [6:0] A_PTP_CTRL   = 7'd64;
    localparam [6:0] A_PTP_TIME_LO= 7'd65;
    localparam [6:0] A_PTP_TIME_HI= 7'd66;
    localparam [6:0] A_PTP_INCR   = 7'd67;
    localparam [6:0] A_PTP_RATE   = 7'd68;
    localparam [6:0] A_PTP_STATUS = 7'd69;
    localparam [6:0] A_EVT_NEXT_LO= 7'd70;
    localparam [6:0] A_EVT_NEXT_HI= 7'd71;
    localparam [6:0] A_EVT_PERIOD = 7'd72;
    localparam [6:0] A_EVT_CFG    = 7'd73;
    localparam [6:0] A_EVT_COUNT  = 7'd74;
    localparam [6:0] A_SN_LO      = 7'd76;
    localparam [6:0] A_SN_HI      = 7'd77;
    localparam [6:0] A_SP_LO      = 7'd78;
    localparam [6:0] A_SP_HI      = 7'd79;
    localparam [6:0] A_SP_FRAC    = 7'd80;
    localparam [6:0] A_LINE_PER   = 7'd85;
    localparam [6:0] A_LINE_CFG   = 7'd87;
    localparam [6:0] A_FRAME_LINES= 7'd88;
    localparam [6:0] A_CAP_CFG    = 7'd89;
    localparam [6:0] A_CAP_LO     = 7'd90;
    localparam [6:0] A_CAP_HI     = 7'd91;
    localparam [6:0] A_CAP_META   = 7'd92;
    localparam [6:0] A_CAP_STATUS = 7'd93;

    reg        aw_en, awready_r, wready_r, bvalid_r, arready_r, rvalid_r;
    reg [31:0] rdata_r;
    reg        soft_reset_r;

    reg               en_r;
    reg [1:0]         in_func_r, in_act_r;
    reg               in_inv_r;
    reg [DELAY_W-1:0] in_delay_r;
    reg [DIV_W-1:0]   in_div_r;
    reg               st_en_r, st_inv_r;
    reg [1:0]         st_src_r;
    reg [DELAY_W-1:0] st_delay_r, st_dur_r, st_minon_r;
    reg [2:0]         out_src_r;
    reg               out_inv_r, out_uval_r;
    reg [1:0]         sy_role_r;
    reg               sy_xvsdir_r, sy_xhsdir_r, sy_xvspol_r, sy_xhspol_r, sy_roleby_r;
    reg [1:0]         sy_troute_r;

    reg [2:0]         ptp_ctrl_r;
    reg               load_pulse_r, commit_pulse_r, evt_arm_r;
    reg [31:0]        ptime_lo_r, ptime_hi_r, time_snap_hi;
    reg [31:0]        rate_r;
    reg [31:0]        evt_next_lo_r, evt_next_hi_r, evt_period_r;
    reg               evt_cfg_en_r, evt_pol_r;
    reg [7:0]         evt_width_r;
    reg [31:0]        sn_lo_r, sn_hi_r, sp_lo_r, sp_hi_r, sp_frac_r;
    reg [23:0]        line_per_r, frame_lines_r;
    reg               xhs_en_r;
    reg [4:0]         cap_cfg_r;

    wire [6:0] wr_addr = s_axi_awaddr[8:2];
    wire       wr_hit  = awready_r && wready_r;
    wire       wr_shadow = (wr_addr == A_SN_LO) || (wr_addr == A_SN_HI) || (wr_addr == A_SP_LO) ||
                           (wr_addr == A_SP_HI) || (wr_addr == A_SP_FRAC) || (wr_addr == A_LINE_PER) ||
                           (wr_addr == A_FRAME_LINES);
    wire       wr_hold   = i_commit_busy && wr_shadow;

    always @(posedge aclk) begin
        if (!aresetn) begin
            awready_r<=1'b0; wready_r<=1'b0; bvalid_r<=1'b0; aw_en<=1'b1; soft_reset_r<=1'b0;
            en_r<=1'b0;
            in_func_r<=2'd0; in_act_r<=2'd0; in_inv_r<=1'b0; in_delay_r<=0; in_div_r<=0;
            st_en_r<=1'b0; st_inv_r<=1'b0; st_src_r<=2'd0;
            st_delay_r<=0; st_dur_r<=0; st_minon_r<=0;
            out_src_r<=3'd0; out_inv_r<=1'b0; out_uval_r<=1'b0;
            sy_role_r<=2'd0; sy_xvsdir_r<=1'b0; sy_xhsdir_r<=1'b0;
            sy_xvspol_r<=1'b0; sy_xhspol_r<=1'b0; sy_roleby_r<=1'b0;
            sy_troute_r<=2'd0;
            ptp_ctrl_r<=3'd0; load_pulse_r<=1'b0; commit_pulse_r<=1'b0; evt_arm_r<=1'b0;
            ptime_lo_r<=32'd0; ptime_hi_r<=32'd0; rate_r<=32'd0;
            evt_next_lo_r<=32'd0; evt_next_hi_r<=32'd0; evt_period_r<=32'd0;
            evt_cfg_en_r<=1'b0; evt_pol_r<=1'b0; evt_width_r<=8'd0;
            sn_lo_r<=32'd0; sn_hi_r<=32'd0; sp_lo_r<=32'd0; sp_hi_r<=32'd0; sp_frac_r<=32'd0;
            line_per_r<=24'd0; frame_lines_r<=24'd0; xhs_en_r<=1'b0;
            cap_cfg_r<=5'd0;
        end else begin
            soft_reset_r   <= 1'b0;
            load_pulse_r   <= 1'b0;
            commit_pulse_r <= 1'b0;
            evt_arm_r      <= 1'b0;

            if (~awready_r && s_axi_awvalid && s_axi_wvalid && aw_en && ~wr_hold) begin
                awready_r <= 1'b1; aw_en <= 1'b0;
            end else awready_r <= 1'b0;

            if (~wready_r && s_axi_wvalid && s_axi_awvalid && aw_en && ~wr_hold)
                wready_r <= 1'b1;
            else
                wready_r <= 1'b0;

            if (wr_hit) begin
                case (wr_addr)
                    A_CONTROL:     begin en_r <= s_axi_wdata[0]; soft_reset_r <= s_axi_wdata[1]; end
                    A_IN_CFG:      begin in_func_r <= s_axi_wdata[1:0]; in_act_r <= s_axi_wdata[3:2]; in_inv_r <= s_axi_wdata[4]; end
                    A_IN_DELAY:    in_delay_r  <= s_axi_wdata[DELAY_W-1:0];
                    A_IN_DIVIDER:  in_div_r    <= s_axi_wdata[DIV_W-1:0];
                    A_ST_CFG:      begin st_en_r <= s_axi_wdata[0]; st_inv_r <= s_axi_wdata[1];
                                         st_src_r <= s_axi_wdata[3:2]; end
                    A_ST_DELAY:    st_delay_r  <= s_axi_wdata[DELAY_W-1:0];
                    A_ST_DUR:      st_dur_r    <= s_axi_wdata[DELAY_W-1:0];
                    A_ST_MINON:    st_minon_r  <= s_axi_wdata[DELAY_W-1:0];
                    A_OUT_CFG:     begin out_src_r <= s_axi_wdata[2:0]; out_inv_r <= s_axi_wdata[3]; out_uval_r <= s_axi_wdata[4]; end
                    A_SYNC_CFG:    begin sy_role_r <= s_axi_wdata[1:0]; sy_xvsdir_r <= s_axi_wdata[2]; sy_xhsdir_r <= s_axi_wdata[3];
                                         sy_xvspol_r <= s_axi_wdata[4]; sy_xhspol_r <= s_axi_wdata[5]; sy_roleby_r <= s_axi_wdata[6]; end
                    A_SYNC_TROUTE: sy_troute_r <= s_axi_wdata[1:0];
                    A_PTP_CTRL:    begin ptp_ctrl_r <= s_axi_wdata[2:0];
                                         load_pulse_r <= s_axi_wdata[3]; commit_pulse_r <= s_axi_wdata[4]; end
                    A_PTP_TIME_LO: ptime_lo_r    <= s_axi_wdata;
                    A_PTP_TIME_HI: ptime_hi_r    <= s_axi_wdata;
                    A_PTP_RATE:    rate_r        <= s_axi_wdata;
                    A_EVT_NEXT_LO: evt_next_lo_r <= s_axi_wdata;
                    A_EVT_NEXT_HI: begin evt_next_hi_r <= s_axi_wdata; evt_arm_r <= 1'b1; end
                    A_EVT_PERIOD:  evt_period_r  <= s_axi_wdata;
                    A_EVT_CFG:     begin evt_cfg_en_r <= s_axi_wdata[0]; evt_pol_r <= s_axi_wdata[1];
                                         evt_width_r <= s_axi_wdata[15:8]; end
                    A_SN_LO:       sn_lo_r       <= s_axi_wdata;
                    A_SN_HI:       sn_hi_r       <= s_axi_wdata;
                    A_SP_LO:       sp_lo_r       <= s_axi_wdata;
                    A_SP_HI:       sp_hi_r       <= s_axi_wdata;
                    A_SP_FRAC:     sp_frac_r     <= s_axi_wdata;
                    A_LINE_PER:    line_per_r    <= s_axi_wdata[23:0];
                    A_LINE_CFG:    xhs_en_r      <= s_axi_wdata[0];
                    A_FRAME_LINES: frame_lines_r <= s_axi_wdata[23:0];
                    A_CAP_CFG:     cap_cfg_r     <= s_axi_wdata[4:0];
                    default: ;
                endcase
            end

            if (wr_hit && ~bvalid_r) bvalid_r <= 1'b1;
            else if (bvalid_r && s_axi_bready) begin bvalid_r <= 1'b0; aw_en <= 1'b1; end
        end
    end

    wire [6:0] rd_addr = s_axi_araddr[8:2];
    wire       rd_fire = ~arready_r && s_axi_arvalid && ~rvalid_r;

    always @(posedge aclk) begin
        if (!aresetn) begin
            arready_r <= 1'b0; rvalid_r <= 1'b0; rdata_r <= 32'd0; time_snap_hi <= 32'd0;
        end else begin
            if (rd_fire) begin
                arready_r <= 1'b1;
                case (rd_addr)
                    A_VERSION:     rdata_r <= VERSION_VAL;
                    A_CONTROL:     rdata_r <= {31'd0, en_r};
                    A_STATUS:      rdata_r <= i_status;
                    A_IN_CFG:      rdata_r <= {27'd0, in_inv_r, in_act_r, in_func_r};
                    A_IN_DELAY:    rdata_r <= {{(32-DELAY_W){1'b0}}, in_delay_r};
                    A_IN_DIVIDER:  rdata_r <= {{(32-DIV_W){1'b0}}, in_div_r};
                    A_IN_STATUS:   rdata_r <= i_in_status;
                    A_ST_CFG:      rdata_r <= {25'd0, 3'b000, st_src_r, st_inv_r, st_en_r};
                    A_ST_DELAY:    rdata_r <= {{(32-DELAY_W){1'b0}}, st_delay_r};
                    A_ST_DUR:      rdata_r <= {{(32-DELAY_W){1'b0}}, st_dur_r};
                    A_ST_MINON:    rdata_r <= {{(32-DELAY_W){1'b0}}, st_minon_r};
                    A_OUT_CFG:     rdata_r <= {27'd0, out_uval_r, out_inv_r, out_src_r};
                    A_SYNC_CFG:    rdata_r <= {25'd0, sy_roleby_r, sy_xhspol_r, sy_xvspol_r, sy_xhsdir_r, sy_xvsdir_r, sy_role_r};
                    A_SYNC_TROUTE: rdata_r <= {30'd0, sy_troute_r};
                    A_SYNC_STATUS: rdata_r <= i_sync_status;
                    A_PTP_CTRL:    rdata_r <= {29'd0, ptp_ctrl_r};
                    A_PTP_TIME_LO: begin rdata_r <= i_time[31:0]; time_snap_hi <= i_time[63:32]; end
                    A_PTP_TIME_HI: rdata_r <= time_snap_hi;
                    A_PTP_INCR:    rdata_r <= INC_FRAC;
                    A_PTP_RATE:    rdata_r <= rate_r;
                    A_PTP_STATUS:  rdata_r <= i_ptp_status;
                    A_EVT_NEXT_LO: rdata_r <= evt_next_lo_r;
                    A_EVT_NEXT_HI: rdata_r <= evt_next_hi_r;
                    A_EVT_PERIOD:  rdata_r <= evt_period_r;
                    A_EVT_CFG:     rdata_r <= {16'd0, evt_width_r, 6'd0, evt_pol_r, evt_cfg_en_r};
                    A_EVT_COUNT:   rdata_r <= i_evt_count;
                    A_SN_LO:       rdata_r <= sn_lo_r;
                    A_SN_HI:       rdata_r <= sn_hi_r;
                    A_SP_LO:       rdata_r <= sp_lo_r;
                    A_SP_HI:       rdata_r <= sp_hi_r;
                    A_SP_FRAC:     rdata_r <= sp_frac_r;
                    A_LINE_PER:    rdata_r <= {8'd0, line_per_r};
                    A_LINE_CFG:    rdata_r <= {31'd0, xhs_en_r};
                    A_FRAME_LINES: rdata_r <= {8'd0, frame_lines_r};
                    A_CAP_CFG:     rdata_r <= {27'd0, cap_cfg_r};
                    A_CAP_LO:      rdata_r <= i_cap_head_lo;
                    A_CAP_HI:      rdata_r <= i_cap_hold_hi;
                    A_CAP_META:    rdata_r <= i_cap_hold_meta;
                    A_CAP_STATUS:  rdata_r <= i_cap_status;
                    default:       rdata_r <= 32'd0;
                endcase
            end else arready_r <= 1'b0;

            if (arready_r && ~rvalid_r) rvalid_r <= 1'b1;
            else if (rvalid_r && s_axi_rready) rvalid_r <= 1'b0;
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

    assign o_enable            = en_r;
    assign o_soft_reset        = soft_reset_r;
    assign o_in_func           = in_func_r;
    assign o_in_activation     = in_act_r;
    assign o_in_invert         = in_inv_r;
    assign o_in_delay          = in_delay_r;
    assign o_in_divider        = in_div_r;
    assign o_strobe_en         = st_en_r;
    assign o_strobe_invert     = st_inv_r;
    assign o_strobe_src        = st_src_r;
    assign o_strobe_delay      = st_delay_r;
    assign o_strobe_dur        = st_dur_r;
    assign o_strobe_minon      = st_minon_r;
    assign o_out_src           = out_src_r;
    assign o_out_invert        = out_inv_r;
    assign o_out_user_value    = out_uval_r;
    assign o_sync_role         = sy_role_r;
    assign o_xvs_dir           = sy_xvsdir_r;
    assign o_xhs_dir           = sy_xhsdir_r;
    assign o_xvs_pol           = sy_xvspol_r;
    assign o_xhs_pol           = sy_xhspol_r;
    assign o_role_by           = sy_roleby_r;
    assign o_sync_trig_route   = sy_troute_r;
    assign o_xhs_en            = xhs_en_r;
    assign o_cap_src           = cap_cfg_r[2:0];
    assign o_cap_edge          = cap_cfg_r[3];

    assign o_tb_en             = ptp_ctrl_r[0];
    assign o_evt_en            = ptp_ctrl_r[1] & evt_cfg_en_r;
    assign o_sched_en          = ptp_ctrl_r[2];
    assign o_load_pulse        = load_pulse_r;
    assign o_load_val          = {ptime_hi_r, ptime_lo_r};
    assign o_commit_pulse      = commit_pulse_r;
    assign o_rate_adj          = rate_r;
    assign o_evt_arm           = evt_arm_r;
    assign o_evt_next          = {evt_next_hi_r, evt_next_lo_r};
    assign o_evt_period        = evt_period_r;
    assign o_evt_width         = evt_width_r;
    assign o_evt_pol           = evt_pol_r;
    assign o_sched_next        = {sn_hi_r, sn_lo_r};
    assign o_sched_period      = {sp_hi_r, sp_lo_r};
    assign o_sched_period_frac = sp_frac_r;
    assign o_line_period       = line_per_r;
    assign o_frame_lines       = frame_lines_r;
    assign o_cap_en            = cap_cfg_r[4];
    assign o_cap_latch         = rd_fire && (rd_addr == A_CAP_LO);
    assign o_cap_pop           = rd_fire && (rd_addr == A_CAP_META);

endmodule
