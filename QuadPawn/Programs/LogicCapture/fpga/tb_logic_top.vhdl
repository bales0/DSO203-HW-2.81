library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity tb_logic_top is end entity;
architecture testbench of tb_logic_top is
    signal clk: std_logic := '0';signal rst_n: std_logic := '0';
    signal ce: std_logic := '0';signal nrd: std_logic := '1';
    signal select_count,select_id: std_logic := '0';
    signal db: std_logic_vector(15 downto 0);
    signal cpu: std_logic_vector(15 downto 0) := (others=>'Z');
    signal a,b: std_logic_vector(7 downto 0) := X"80";
    signal c,d: std_logic := '0';signal sleep,clka,clkb: std_logic;
    type states_t is array(0 to 7) of natural;
    constant states: states_t := (1,3,7,15,14,12,8,0);
begin
    db<=cpu;
    dut: entity work.fpga_top generic map(fifo_depth_g=>32)
        port map(clk,rst_n,ce,nrd,db,select_count,select_id,sleep,clka,clkb,a,b,c,d);
    process
        procedure clock(n: natural:=1) is
        begin
            for i in 1 to n loop clk<='0';wait for 6944 ps;clk<='1';wait for 6944 ps;end loop;
        end procedure;
        procedure config(v:natural) is
        begin
            -- No parallel writes: GPIO selection must suffice for every mode.
            if v mod 2=1 then select_count<='1';else select_count<='0';end if;
            if v>=32 then select_id<='1';else select_id<='0';end if;
            clock(4);
        end procedure;
        procedure read_bus(expected:natural) is
        begin
            ce<='1';nrd<='0';clock(4);
            assert db=std_logic_vector(to_unsigned(expected,16)) report "Bus data mismatch" severity failure;
            nrd<='1';clock(4);ce<='0';clock(2);
            assert db=(db'range=>'Z') report "Data bus not released" severity failure;
        end procedure;
        variable value:std_logic_vector(15 downto 0);
    begin
        clock(4);rst_n<='1';clock(8);config(32);read_bus(16#4C43#);
        -- Shared LCD bus traffic cannot change the selected register, drive
        -- against the LCD or remove a capture word. CE is NE1, low for LCD.
        ce<='0';cpu<=X"A55A";clock(4);nrd<='0';clock(4);
        assert db=X"A55A" report "FPGA drives LCD transaction" severity failure;
        nrd<='1';clock(4);cpu<=(others=>'Z');clock(4);read_bus(16#4C43#);
        a(7)<='0';clock(128);b(7)<='0';clock(128);c<='1';clock(128);d<='1';clock(128);
        a(7)<='1';clock(128);b(7)<='1';clock(128);c<='0';clock(128);d<='0';clock(32);
        config(1);read_bus(8);read_bus(8);config(32);read_bus(16#4C43#);config(1);read_bus(8);
        config(0);
        for i in states'range loop
            ce<='1';nrd<='0';clock(4);value:=db;
            assert value(15)='0' and to_integer(unsigned(value(3 downto 0)))=states(i)
                report "Channel polarity/order or RLE encoding wrong" severity failure;
            nrd<='1';clock(4);ce<='0';clock(2);
        end loop;
        config(1);read_bus(0);
        -- FIFO full count, one pop per transaction, and reset recovery.
        for i in 1 to 100 loop c<=not c;clock(8);end loop;
        clock(8);config(1);read_bus(16#801F#);read_bus(16#801F#);
        config(0);ce<='1';nrd<='0';clock(4);nrd<='1';clock(4);ce<='0';clock(4);
        config(1);read_bus(16#801E#); -- Full flag survives draining.
        rst_n<='0';clock(4);c<='0';rst_n<='1';clock(12);
        config(1);read_bus(0);config(32);read_bus(16#4C43#);
        assert sleep='1' and clka=clk and clkb=clk report "ADC control outputs wrong" severity failure;
        report "PASS Logic top: four channels, RLE, identity, FIFO count/pop/full, reset and bus release" severity note;
        wait;
    end process;
end architecture;
