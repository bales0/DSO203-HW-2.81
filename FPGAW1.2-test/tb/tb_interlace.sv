`timescale 1ns/1ps
module tb_interlace;
    reg ClrW = 1, MCI = 0, nRD = 1, CE = 1;
    reg H_L = 0, C_D = 0, SCK = 1, SDA = 0;
    reg [7:0] adc_a = 0, adc_b = 0;
    reg [7:0] a_value, b_value;
    wire [17:0] Din = {2'b00, ~adc_b, ~adc_a};
    wire nPD, CKA, CKB;
    tri [15:0] DB;
    integer i, phase_sample, previous_a;
    reg [15:0] word_value;

    FIFO dut(
        .ClrW(ClrW), .MCI(MCI), .nRD(nRD), .CE(CE),
        .H_L(H_L), .C_D(C_D), .SCK(SCK), .SDA(SDA),
        .Din(Din), .nPD(nPD), .CKA(CKA), .CKB(CKB), .DB(DB)
    );
    always #7 MCI = ~MCI; // 14 ns period, approximately 72 MHz.

    // Model ADC output becoming valid one nanosecond after its clock edge.
    // Both ADCs receive the same synthetic analog signal: sample index
    // advances every seven nanoseconds, so a half-period offset is visible.
    always @(posedge CKA) begin
        a_value = ($time - 7) / 7;
        #1 adc_a <= a_value;
    end
    always @(posedge CKB) begin
        b_value = ($time - 7) / 7;
        #1 adc_b <= b_value;
    end

    task automatic send_byte(input [7:0] value, input bit address_phase);
        integer bit_index;
        begin
            H_L = address_phase;
            for (bit_index = 7; bit_index >= 0; bit_index = bit_index - 1) begin
                SDA = value[bit_index];
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

    task automatic write_reg(input [7:0] address, input [7:0] value);
        begin
            send_byte(address, 1);
            send_byte(value, 0);
        end
    endtask

    task automatic capture_and_check(input bit interlaced);
        integer sample_a, sample_b;
        begin
            ClrW = 0;
            #1 ClrW = 1;
            repeat (3) @(negedge MCI);
            ClrW = 0;
            for (i = 0; i < 4400 && dut.Full_Link !== 1'b1; i = i + 1)
                @(negedge MCI);
            if (dut.Full_Link !== 1'b1)
                $fatal(1, "FIFO did not fill in mode %0d", interlaced);

            // First H_L read loads the pre-trigger read pointer.
            H_L = 1;
            #2 nRD = 0;
            #2 nRD = 1;
            // Discard invalid pre-trigger history in unconditional mode.
            repeat (152) begin
                #2 nRD = 0;
                #2 nRD = 1;
            end
            previous_a = -1;
            for (i = 0; i < 32; i = i + 1) begin
                #2 nRD = 0;
                #2 word_value = DB;
                if (^word_value === 1'bx) begin
                    $fatal(1, "Unknown FIFO output in valid window, mode %0d sample %0d", interlaced, i);
                end
                sample_a = word_value[7:0];
                sample_b = word_value[15:8];
                if (interlaced) begin
                    if (sample_b !== ((sample_a + 1) & 8'hff))
                        $fatal(1, "interlaced pair %0d: A=%0d B=%0d", i, sample_a, sample_b);
                end else if (sample_b !== sample_a) begin
                    $fatal(1, "separate pair %0d: A=%0d B=%0d", i, sample_a, sample_b);
                end
                if (previous_a >= 0 && sample_a !== ((previous_a + 2) & 8'hff))
                    $fatal(1, "sample order %0d: previous A=%0d A=%0d", i, previous_a, sample_a);
                previous_a = sample_a;
                nRD = 1;
            end
            H_L = 0;
        end
    endtask

    initial begin
        // Explicit simulation power-up state for counters lacking HDL reset.
        // FIFO RAM is not initialized; only the post-trigger window is valid.
        dut.U1.Wptr = 0;
        dut.U2.TrigBufAddr = 0;
        write_reg(8'h0e, 8'h00); // FreeRun off.
        write_reg(8'h0f, 8'h00); // Oversampling off.
        write_reg(8'h10, 8'h00);
        write_reg(8'h00, 8'h20); // Unconditional trigger, 150 pre-samples.
        write_reg(8'h04, 8'h03); // Power on and inverted ADC B clock.
        @(negedge MCI);
        #1;
        if (CKB !== ~CKA) $fatal(1, "interlaced clock phase missing");
        capture_and_check(1);
        write_reg(8'h04, 8'h01); // Return to separate ADC clocks.
        @(negedge MCI);
        #1;
        if (CKB !== CKA) $fatal(1, "separate clock phase missing");
        capture_and_check(0);
        $display("PASS tb_interlace: phase and 32 ordered FIFO pairs in both modes");
        $display("External analog mux, ADC timing, calibration and 144 MSa/s on hardware NOT TESTED");
        $finish;
    end
endmodule
