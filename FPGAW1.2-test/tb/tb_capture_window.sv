`timescale 1ns/1ps
module tb_capture_window;
    reg ClrW=1, MCI=0, nRD=1, CE=1, H_L=0, C_D=0, SCK=1, SDA=0;
    reg [7:0] new_a=0, new_b=0;
    wire [17:0] Din={2'b00, ~new_b, ~new_a};
    wire nPD, CKA, CKB;
    tri [15:0] DB;
    FIFO dut(.ClrW(ClrW),.MCI(MCI),.nRD(nRD),.CE(CE),.H_L(H_L),
        .C_D(C_D),.SCK(SCK),.SDA(SDA),.Din(Din),.nPD(nPD),
        .CKA(CKA),.CKB(CKB),.DB(DB));
    always #7 MCI=~MCI;
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


    task automatic read_pair(output [15:0] value);
        begin
            #2 nRD=0;
            #2 value=DB;
            nRD=1;
            #2;
        end
    endtask

    task automatic check_frame(input [15:0] expected, input [15:0] old_value);
        integer j, stale;
        reg [15:0] value;
        begin
            H_L=0;
            new_a=expected[7:0]; new_b=expected[15:8];
            // Let the ADC-input pipeline settle while capture is held reset.
            ClrW=0;
            #1 ClrW=1;
            repeat (20) @(negedge MCI);
            ClrW=0;
            for(j=0;j<4400 && dut.Full_Link!==1'b1;j=j+1) @(negedge MCI);
            if(dut.Full_Link!==1'b1) $fatal(1,"Capture did not fill");
            H_L=1;
            read_pair(value); // Prime the pre-trigger pointer.
            stale=0;
            for(j=0;j<152;j=j+1) begin
                read_pair(value);
                if(value===old_value) stale=stale+1;
            end
            if(stale==0) $fatal(1,"Test did not expose old pre-trigger history");
            for(j=0;j<512;j=j+1) begin
                read_pair(value);
                if(^value===1'bx) $fatal(1,"Unknown retained sample %0d",j);
                if(value!==expected) $fatal(1,"Old/incorrect retained sample %0d: %h expected %h",j,value,expected);
            end
            $display("PASS frame %h: %0d old pairs discarded, 512 fresh pairs retained",expected,stale);
            H_L=0;
        end
    endtask

    integer index;
    initial begin
        // Model a deterministic prior capture; this is testbench state only.
        dut.U1.Wptr=0;
        dut.U2.TrigBufAddr=0;
        for(index=0;index<4096;index=index+1) dut.U2.Mem_data[index]=18'h0c719;
        write_reg(8'h0e,0);
        write_reg(8'h0f,0);
        write_reg(8'h10,0);
        write_reg(8'h00,8'h20);
        write_reg(8'h04,8'h01);
        check_frame(16'ha63a,16'hc719);
        check_frame(16'h9961,16'ha63a);
        $display("PASS tb_capture_window: two unconditional captures contain no old data after discard");
        $finish;
    end
endmodule
