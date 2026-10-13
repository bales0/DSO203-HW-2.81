module tb_io_ctrl;
    reg CE = 1, nRD = 1, SCK = 1, SDA = 0;
    reg H_L = 0, C_D = 1;
    reg [17:0] Dout = 18'h2a55a;
    reg Start = 0, Full = 0, Empty = 1, Ready = 0;
    wire [15:0] DB, PerCnt, OS_Size;
    wire nPD, FreeRun;
    wire [7:0] Trigg_Mode, Vthreshold, CtrlReg;
    wire [31:0] XTthreshold;

    IO_Ctrl dut(
        .CE(CE), .nRD(nRD), .SCK(SCK), .SDA(SDA), .Dout(Dout),
        .Start(Start), .Full(Full), .Empty(Empty), .H_L(H_L),
        .C_D(C_D), .Ready(Ready), .PerCnt(PerCnt), .nPD(nPD),
        .Trigg_Mode(Trigg_Mode), .Vthreshold(Vthreshold),
        .XTthreshold(XTthreshold), .CtrlReg(CtrlReg), .DB(DB),
        .FreeRun(FreeRun), .OS_Size(OS_Size)
    );

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

    task automatic write_reg(input [7:0] address, input [7:0] value);
        begin
            send_byte(address, 1);
            send_byte(value, 0);
        end
    endtask

    initial begin
        write_reg(8'h04, 8'h03);
        if (CtrlReg !== 8'h03 || nPD !== 1'b1)
            $fatal(1, "control register or ADC power control failed");
        write_reg(8'h0f, 8'h02);
        write_reg(8'h10, 8'h01);
        if (OS_Size !== 16'h0102)
            $fatal(1, "oversampling size register failed");
        write_reg(8'h01, 8'h55);
        if (Vthreshold !== 8'h55)
            $fatal(1, "voltage threshold register failed");
        write_reg(8'h02, 8'h12);
        write_reg(8'h03, 8'h34);
        write_reg(8'h11, 8'h56);
        write_reg(8'h12, 8'h78);
        if (XTthreshold !== 32'h78563412)
            $fatal(1, "32-bit trigger time register failed");
        write_reg(8'h05, 8'h00);
        send_byte(8'h00, 1);
        H_L = 0;
        nRD = 0;
        #2;
        if (DB !== 16'h5731)
            $fatal(1, "W1 ID read failed: %h", DB);
        H_L = 1;
        #2;
        if (DB !== Dout[15:0])
            $fatal(1, "FIFO data bus read failed");
        CE = 0;
        #2;
        if (DB !== 16'hzzzz)
            $fatal(1, "FIFO bus must be high impedance when disabled");
        $display("PASS tb_io_ctrl: registers, ID, FIFO bus");
        $finish;
    end
endmodule
