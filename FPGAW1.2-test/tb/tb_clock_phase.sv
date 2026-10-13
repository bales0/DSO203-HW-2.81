module tb_clock_phase;
    reg ClrW = 1, MCI = 0, nRD = 1, CE = 0;
    reg H_L = 0, C_D = 0, SCK = 1, SDA = 0;
    reg [17:0] Din = 18'h15555;
    wire nPD, CKA, CKB;
    tri [15:0] DB;

    FIFO dut(
        .ClrW(ClrW), .MCI(MCI), .nRD(nRD), .CE(CE),
        .H_L(H_L), .C_D(C_D), .SCK(SCK), .SDA(SDA),
        .Din(Din), .nPD(nPD), .CKA(CKA), .CKB(CKB), .DB(DB)
    );
    always #7 MCI = ~MCI;

    task automatic send_byte(input [7:0] value, input bit address_phase);
        integer i;
        begin
            H_L = address_phase;
            for (i = 7; i >= 0; i = i - 1) begin
                SDA = value[i];
                #2 SCK = 0;
                #2 SCK = 1;
                #2;
            end
            SDA = 0;
            #2 SCK = 0;
            #2 SDA = 1;
            #2 SDA = 0;
            #2 SCK = 1;
        end
    endtask

    task automatic check_clocks(input bit inverted);
        integer i;
        begin
            for (i = 0; i < 12; i = i + 1) begin
                @(posedge MCI);
                #1;
                if (CKA !== MCI || CKB !== (inverted ? ~MCI : MCI))
                    $fatal(1, "ADC clock phase failed after rising edge %0d", i);
                @(negedge MCI);
                #1;
                if (CKA !== MCI || CKB !== (inverted ? ~MCI : MCI))
                    $fatal(1, "ADC clock phase failed after falling edge %0d", i);
            end
        end
    endtask

    initial begin
        #3 ClrW = 0;
        send_byte(8'h04, 1);
        send_byte(8'h03, 0);
        check_clocks(1);
        send_byte(8'h04, 1);
        send_byte(8'h01, 0);
        check_clocks(0);
        $display("PASS tb_clock_phase: ADC clock inversion prerequisite only");
        $display("144 MSa/s interleaving, signal routing, and calibration NOT TESTED");
        $finish;
    end
endmodule
