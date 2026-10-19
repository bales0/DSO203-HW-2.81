`timescale 1ns/1ps
module tb_capture_window;
    reg ClrW=1, MCI=0, nRD=1, CE=1, H_L=0, C_D=0, SCK=1, SDA=0;
    reg [7:0] new_a=0, new_b=0;
    reg [1:0] new_cd=0;
    wire [17:0] Din={new_cd, ~new_b, ~new_a};
    wire nPD, CKA, CKB;
    tri [15:0] DB;
    FIFO dut(.ClrW(ClrW),.MCI(MCI),.nRD(nRD),.CE(CE),.H_L(H_L),
        .C_D(C_D),.SCK(SCK),.SDA(SDA),.Din(Din),.nPD(nPD),
        .CKA(CKA),.CKB(CKB),.DB(DB));
    always #7 MCI=~MCI;

    // Digital FIFO-input model, NOT an analog ADC timing model. tb_interlace
    // separately checks the ADC clocks. Here every input word has a serial
    // identity, including C/D, so a 256-sample repetition cannot hide a wrap.
    bit marked=0, edge_pattern=0, stim_high=0;
    integer serial=0, arm_serial, edge_serial, start_serial, ready_serial;
    integer checks=0, scenarios=0, write_wraps=0, prime_wraps=0;
    reg [17:0] input_word, model_word[0:4095], pipe_word[0:3];
    integer model_serial[0:4095], pipe_serial[0:3];
    integer observed_serial[0:4095];
    reg [17:0] observed_word[0:4095];
    bit previous_high=0;

    // Each sampled Din passes through TrigBuf (two clocks), A/B/CD (one),
    // and ACC/Multi (one). This independent four-clock scoreboard never
    // reads Mem_data as its oracle and never forces trigger/address logic.
    always @(negedge MCI) if(marked) begin
        serial=serial+1;
        if(edge_pattern) begin
            input_word={2'b00,8'b0,8'b0};
            input_word[7:0]=(stim_high ? 160 : 64)+(serial & 31);
            input_word[15:8]=(serial >> 5) & 255;
            input_word[17:16]=(serial ^ (serial >> 3)) & 3;
            if(dut.Tm_Link[4:3]==2) input_word[16]=stim_high;
            if(dut.Tm_Link[4:3]==3) input_word[17]=stim_high;
        end else begin
            input_word=serial & 18'h3ffff;
            input_word[17:16]=(serial ^ (serial >> 3)) & 3;
        end
        new_a=input_word[7:0]; new_b=input_word[15:8];
        new_cd=input_word[17:16];
        if(stim_high!=previous_high) edge_serial=serial;
        previous_high=stim_high;
    end

    always @(posedge MCI) if(marked) begin
        if(dut.Counter_Link===0) begin
            model_word[dut.W_Addr_Bus]=pipe_word[3];
            model_serial[dut.W_Addr_Bus]=pipe_serial[3];
            if(!ClrW && !dut.Full_Link && dut.W_Addr_Bus==4095)
                write_wraps=write_wraps+1;
        end
        pipe_word[3]=pipe_word[2]; pipe_serial[3]=pipe_serial[2];
        pipe_word[2]=pipe_word[1]; pipe_serial[2]=pipe_serial[1];
        pipe_word[1]=pipe_word[0]; pipe_serial[1]=pipe_serial[0];
        pipe_word[0]={new_cd,new_b,new_a}; pipe_serial[0]=serial;
    end

    always @(posedge dut.Start_Link) if(marked) start_serial=serial;
    always @(posedge dut.Ready_Link) if(marked) ready_serial=serial;

    task automatic require(input bit condition, input string reason);
        begin
            checks=checks+1;
            if(!condition) $fatal(1,"%s (scenario %0d, serial %0d)",reason,scenarios,serial);
        end
    endtask

    // Global watchdog complements every bounded per-scenario wait.
    initial begin
        #40000000;
        $fatal(1,"tb_capture_window global watchdog");
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

    task automatic read_status(output [15:0] value);
        integer address_before;
        begin
            @(negedge MCI); #1;
            address_before=dut.R_Addr_Bus;
            H_L=0; nRD=0;
            #2 value=DB;
            nRD=1; #1;
            require(dut.R_Addr_Bus==address_before,"status read advanced Rptr");
        end
    endtask

    task automatic reset_record(input [7:0] mode, input integer pre_count,
        input bit use_edge, input bit initial_high);
        begin
            scenarios=scenarios+1;
            @(negedge MCI); #1 ClrW=1;
            H_L=0; edge_pattern=use_edge; stim_high=initial_high;
            write_reg(8'h00,mode);
            require(dut.PerCnt_Link==150,"trigger write did not restore PerCnt=150");
            write_reg(8'h01,128);
            write_reg(8'h02,32); write_reg(8'h03,0);
            write_reg(8'h11,0); write_reg(8'h12,0);
            write_reg(8'h08,pre_count & 255);
            write_reg(8'h09,(pre_count >> 8) & 255);
            require(dut.PerCnt_Link==pre_count,"PerCnt low/high byte writes lost after TRIGG_MODE");
            write_reg(8'h0e,0); // FreeRun must be explicit for each record.
            write_reg(8'h04,3); // Interlaced clock configuration.
            H_L=0;
            repeat(24) @(negedge MCI);
            #1;
            require(dut.Start_Link===0 && dut.Ready_Link===0 && dut.Full_Link===0,
                "ClrW did not clear Start/Ready/Full");
            require(dut.U1.Loaded===0 && dut.R_Addr_Bus===0,"ClrW did not clear reader");
            // Current negedge's input is the first one sampled after release.
            arm_serial=serial;
            edge_serial=-1; start_serial=-1; ready_serial=-1;
            ClrW=0;
        end
    endtask

    task automatic wait_full;
        integer elapsed;
        reg [15:0] status;
        begin
            for(elapsed=0;elapsed<4400 && dut.Full_Link!==1'b1;elapsed=elapsed+1)
                @(negedge MCI);
            require(dut.Full_Link===1'b1,"bounded wait: FIFO did not fill");
            // Deliberately leave MCI running. Full stops Wptr, not RAM writes.
            repeat(20) @(negedge MCI);
            read_status(status);
            require((status & 16'h002c)==16'h002c,"bus Start/Full/Ready flags missing");
            require(dut.U1.Loaded===0,"status read unexpectedly primed reader");
        end
    endtask

    task automatic read_full_frame(input bit hardware_trigger,
        input integer pre_count, input bit rising);
        integer j, address_before, initial_address, expected_serial;
        integer valid_begin, edge_index, wraps_before;
        bit contiguous;
        reg [15:0] value, status;
        reg [17:0] expected;
        begin
            initial_address=(dut.U1.Bptr-pre_count) & 4095;
            if(dut.U1.Bptr<pre_count) prime_wraps=prime_wraps+1;
            H_L=1;
            read_pair(value); // Exactly one pointer-loading read, not a data word.
            require(dut.U1.Loaded===1,"priming did not load reader");
            require(dut.R_Addr_Bus==initial_address,"priming did not select Bptr-PerCnt");
            wraps_before=write_wraps;
            for(j=0;j<4096;j=j+1) begin
                @(negedge MCI); #1;
                address_before=dut.R_Addr_Bus;
                expected=model_word[address_before];
                expected_serial=model_serial[address_before];
                H_L=1; nRD=0;
                #2 value=DB;
                // Sample C/D/status from the same Dout before the rising
                // read edge advances Rptr. A second falling edge would latch
                // another RAM word and must not be used to pair these bits.
                H_L=0;
                #1 status=DB;
                H_L=1;
                #1 nRD=1;
                #1;
                observed_word[j]={status[1:0],value};
                observed_serial[j]=expected_serial;
                require(observed_word[j]===expected,
                    $sformatf("18-bit bus/independent pipeline mismatch at word %0d: %h != %h",j,observed_word[j],expected));
                require(dut.R_Addr_Bus==((address_before+1)&4095),"data read did not advance exactly once");
                require((status & 16'h002c)==16'h002c,"capture flags changed during full read");
            end
            require(dut.R_Addr_Bus==initial_address,"4096 reads did not wrap exactly once");
            require(write_wraps==wraps_before,"Wptr advanced after Full");

            // Find the maximal fresh, chronological suffix; never label the
            // physical 4096-word capacity as 4096 valid words by assumption.
            valid_begin=4096; contiguous=1;
            for(j=4095;j>=0;j=j-1) begin
                if(observed_serial[j]<arm_serial) contiguous=0;
                if(j<4095 && observed_serial[j]+1!=observed_serial[j+1]) contiguous=0;
                if(contiguous) valid_begin=j;
            end
            require(valid_begin<4096,"no fresh chronological suffix");
            for(j=valid_begin;j<4096;j=j+1) begin
                require(observed_serial[j]>=arm_serial,"retained sample predates this record");
                if(j>valid_begin) require(observed_serial[j]==observed_serial[j-1]+1,
                    "retained sequence is not monotonic");
            end
            // Full's transition writes one additional word at the wrapped
            // start; subsequent clocks keep rewriting the following word.
            // Neither belongs at the beginning of a chronological frame.
            require(observed_serial[0]==observed_serial[4095]+1 &&
                observed_serial[1]>observed_serial[0],
                "fixture did not expose wrap/overwrite at stopped Wptr");
            $display("TRACE mode=%h arm=%0d edge=%0d Start=%0d Ready=%0d Bptr=%0d Wptr=%0d suffix=%0d first=%0d second=%0d tail=%0d",
                dut.Tm_Link,arm_serial,edge_serial,start_serial,ready_serial,dut.U1.Bptr,dut.W_Addr_Bus,valid_begin,
                observed_serial[0],observed_serial[1],observed_serial[4095]);
            if(hardware_trigger) begin
                require(valid_begin==2,"mature HW trigger fresh suffix changed");
                edge_index=-1;
                for(j=valid_begin;j<4096;j=j+1)
                    if(edge_index<0 && (!dut.Tm_Link[2] || observed_serial[j]==edge_serial) &&
                        (dut.Tm_Link[4:3]==2 ? observed_word[j][16] :
                         dut.Tm_Link[4:3]==3 ? observed_word[j][17] :
                         observed_word[j][7:0]>128)==rising) edge_index=j;
                require(edge_index>=valid_begin && edge_index<4095,"HW edge absent in valid record");
                require(observed_serial[edge_index]==edge_serial,"edge word lost pipeline identity");
                require(edge_index==pre_count-(dut.Tm_Link[2]?2:1),"HW qualifying edge position (pipeline regression)");
                require(2*(edge_index-valid_begin)+(dut.Tm_Link[4:3]==0?1:0)==
                    2*pre_count-(dut.Tm_Link[4:3]==0?5:6)-(dut.Tm_Link[2]?2:0),
                    "PerCnt to retained B/A time sample mismatch");
                require(2*(4096-valid_begin)==8188,"HW valid interlaced length changed");
                require(start_serial==edge_serial+(dut.Tm_Link[2]?4:3) && ready_serial==edge_serial+(dut.Tm_Link[2]?6:5),
                    "HW trigger pipeline timing changed");
                $display("PASS HW mode %h: Bptr=%0d start=%0d, valid=[%0d,4096)=%0d words, input edge=%0d",
                    dut.Tm_Link,dut.U1.Bptr,initial_address,valid_begin,4096-valid_begin,edge_index);
            end else begin
                require(valid_begin<=152 && valid_begin>1,"AUTO stale prefix not bounded by discard=152");
                for(j=2;j<valid_begin;j=j+1)
                    require(observed_serial[j]<arm_serial,"AUTO prefix was not prior history");
                $display("PASS AUTO: Bptr=%0d start=%0d, maximal fresh suffix=[%0d,4096)=%0d words; conservative discard=152 retains 3944",
                    dut.U1.Bptr,initial_address,valid_begin,4096-valid_begin);
            end
            H_L=0;
        end
    endtask

    // Width predicates use the original Signal.v counters. The event is the
    // closing edge, qualified one clock later than an edge/level trigger.
    task automatic pulse_record_pre(input [7:0] mode, input integer history,
        input integer pulse_width, input bit rising, input bit accepted, input integer pre_count);
        integer j;
        begin
            reset_record(mode,pre_count,1,rising);
            repeat(history) @(negedge MCI);
            #1; require(dut.Sampled_Link===1,"pulse prehistory absent");
            stim_high=!rising;
            repeat(pulse_width) @(negedge MCI);
            #1; require(dut.Start_Link===0,"pulse triggered before closing edge");
            stim_high=rising;
            if(accepted) begin
                wait_full; read_full_frame(1,pre_count,rising);
            end else begin
                for(j=0;j<256;j=j+1) begin
                    @(negedge MCI); #1;
                    require(dut.Start_Link===0 && dut.Ready_Link===0 && dut.Full_Link===0,
                        "rejected pulse published a record");
                end
            end
        end
    endtask

    task automatic pulse_record(input [7:0] mode, input integer history,
        input integer pulse_width, input bit rising, input bit accepted);
        pulse_record_pre(mode,history,pulse_width,rising,accepted,150);
    endtask

    task automatic edge_record(input [7:0] mode, input integer history,
        input integer pre_count, input bit rising);
        integer j;
        begin
            reset_record(mode,pre_count,1,!rising);
            for(j=0;j<history;j=j+1) begin
                @(negedge MCI); #1;
                require(dut.Start_Link===0 && dut.Ready_Link===0 && dut.Full_Link===0,
                    "HW acquisition started before the selected A transition");
            end
            require(dut.Sampled_Link===1,"pretrigger history was not filled");
            stim_high=rising;
            wait_full;
            read_full_frame(1,pre_count,rising);
        end
    endtask

    task automatic timeout_record(input integer pre_count, input bit early_edge,
        input bit free_run);
        integer j, wraps_before;
        reg [15:0] status;
        begin
            reset_record(8'h01,pre_count,1,0);
            if(free_run) begin
                write_reg(8'h0e,1); H_L=0;
            end
            wraps_before=write_wraps;
            for(j=0;j<8500;j=j+1) begin
                @(negedge MCI); #1;
                if(early_edge && j==10) stim_high=1;
                // Without FreeRun no edge means no Ready/Full, indefinitely.
                require(dut.Start_Link===0 && dut.Ready_Link===0,"untriggered timeout became Ready/Start");
                if(!free_run) require(dut.Full_Link===0,"untriggered FIFO became Full");
            end
            if(!free_run) require(write_wraps>wraps_before,"waiting history did not wrap");
            read_status(status);
            require((status & 16'h0024)==0,"timeout status falsely announces captured frame");
            if(free_run) require((status & 8)!=0,"FreeRun did not expose Full without trigger/Ready");
            if(pre_count>=4096) require(dut.Sampled_Link===0,"out-of-range PerCnt unexpectedly Sampled");
            @(negedge MCI); #1 ClrW=1;
            #1;
            require(dut.Start_Link===0 && dut.Ready_Link===0 && dut.Full_Link===0,
                "abort/reset did not clear wait state");
            $display("PASS bounded wait/abort: PerCnt=%0d early-edge=%0d FreeRun=%0d, no false Ready/Start",pre_count,early_edge,free_run);
        end
    endtask

    integer index, pre_index, pre_value, source_index, kind_index;
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
        $display("PASS legacy regression: two unconditional 512-pair windows");

        // Deterministic prior RAM for the independent scoreboard. This is
        // simulation power-up state only, not a firmware initialization claim.
        ClrW=1;
        for(index=0;index<4096;index=index+1) begin
            model_word[index]={2'b00,16'h9961};
            model_serial[index]=-100000+index;
            dut.U2.Mem_data[index]=model_word[index];
        end
        for(index=0;index<4;index=index+1) begin
            pipe_word[index]=0; pipe_serial[index]=-100;
        end
        // Deliberately start near the physical RAM boundary; all subsequent
        // Wptr/Bptr/Rptr values are produced by the unmodified DUT.
        dut.U1.Wptr=4090;
        // Signal.v does not reset these analog edge-history registers. Give
        // simulation a defined power-up model; accepted pulses below include
        // an opening transition that establishes both counter and polarity.
        dut.U3.A_Flag=0; dut.U3.A_Cnt=0;
        marked=1;
        reset_record(8'h20,150,0,0); wait_full; read_full_frame(0,150,0);
        reset_record(8'h20,150,0,0); wait_full; read_full_frame(0,150,0);
        // First accepted edges just after the prehistory gate opens, not
        // only mature records. The valid suffix/pipeline must be identical.
        edge_record(8'h01,152,150,1);
        edge_record(8'h00,153,150,0);
        edge_record(8'h01,220,150,1);
        edge_record(8'h00,5000,150,0); // Mature history includes a full Wptr wrap.
        edge_record(8'h03,220,150,1);  // FPGA high-level transition semantics.
        edge_record(8'h02,220,150,0);
        edge_record(8'h01,220,4,1);   // Edge near the start of the valid window.
        edge_record(8'h01,4300,4094,1); // Edge near its end.
        timeout_record(150,0,0);
        timeout_record(150,1,0); // Edge before Sampled must not create a frame.
        timeout_record(4096,0,0);
        timeout_record(150,0,1); // Full alone is insufficient without Ready/Start.
        // Recovery after timeout/FreeRun must yield a distinct new frame.
        edge_record(8'h01,5000,150,1);
        // Digital C/D use their true 72 MHz edge detector. Raw input edge
        // identity/position and the full 18-bit path are checked above;
        // the firmware's Normal-reference visual delay is a separate policy.
        edge_record(8'h11,152,150,1);
        edge_record(8'h10,5000,150,0);
        edge_record(8'h19,5000,150,1);
        edge_record(8'h18,153,150,0);
        edge_record(8'h11,220,4,1);
        edge_record(8'h19,4300,4094,1);
        edge_record(8'h12,220,150,0);
        edge_record(8'h13,220,150,1);
        edge_record(8'h1a,220,150,0);
        edge_record(8'h1b,220,150,1);
        for(index=0;index<3;index=index+1) begin
            // Sources A, C, D; independent B/alternate are deliberately absent.
            pulse_record((index==0?0:index==1?16:24)+4,220,16,1,1);
            pulse_record((index==0?0:index==1?16:24)+4,5000,64,1,0);
            pulse_record((index==0?0:index==1?16:24)+5,5000,64,1,1);
            pulse_record((index==0?0:index==1?16:24)+5,220,16,1,0);
            pulse_record((index==0?0:index==1?16:24)+6,220,16,0,1);
            pulse_record((index==0?0:index==1?16:24)+6,5000,64,0,0);
            pulse_record((index==0?0:index==1?16:24)+7,5000,64,0,1);
            pulse_record((index==0?0:index==1?16:24)+7,220,16,0,0);
            // Native counter semantics: a pulse of N clocks compares N-1.
            // Exercise the exact boundary, not only clearly short/long cases.
            pulse_record((index==0?0:index==1?16:24)+4,220,32,1,1);
            pulse_record((index==0?0:index==1?16:24)+4,220,33,1,0);
            pulse_record((index==0?0:index==1?16:24)+5,220,33,1,1);
            pulse_record((index==0?0:index==1?16:24)+5,220,32,1,0);
            pulse_record((index==0?0:index==1?16:24)+6,220,32,0,1);
            pulse_record((index==0?0:index==1?16:24)+6,220,33,0,0);
            pulse_record((index==0?0:index==1?16:24)+7,220,33,0,1);
            pulse_record((index==0?0:index==1?16:24)+7,220,32,0,0);
        end
        // Runtime pretrigger candidates: include a nonzero high byte. The
        // DUT produces every pointer and trigger; the independent serial
        // scoreboard above verifies prime + 4096 reads and the fresh suffix.
        for(pre_index=0;pre_index<4;pre_index=pre_index+1) begin
            pre_value=(pre_index==0?150:pre_index==1?183:pre_index==2?235:256);
            for(source_index=0;source_index<3;source_index=source_index+1) begin
                edge_record((source_index==0?0:source_index==1?16:24)+1,
                    pre_value+2,pre_value,1);
                edge_record((source_index==0?0:source_index==1?16:24),
                    pre_value+3,pre_value,0);
                for(kind_index=0;kind_index<4;kind_index=kind_index+1)
                    edge_record((source_index==0?0:source_index==1?16:24)+kind_index,
                        5000,pre_value,(kind_index & 1));
                for(kind_index=4;kind_index<8;kind_index=kind_index+1)
                    pulse_record_pre((source_index==0?0:source_index==1?16:24)+kind_index,
                        5000,(kind_index & 1)?64:16,kind_index<6,1,pre_value);
            end
        end
        require(write_wraps>0,"suite did not exercise Wptr wrap");
        require(prime_wraps>0,"suite did not exercise Bptr-PerCnt modulo wrap");
        $display("PASS tb_capture_window: %0d marked scenarios, %0d assertions, %0d write wraps",scenarios,checks,write_wraps);
        $display("HDL MODEL ONLY: actual DS203 bus timing, analog mux, B-to-A phase and hardware capacity NOT VERIFIED");
        $finish;
    end
endmodule
