-- Integration test of ADC input -> USB decoder/filters -> parallel MCU bus.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity tb_usb_top is end entity;
architecture testbench of tb_usb_top is
    signal clk: std_logic := '0';
    signal rst_n: std_logic := '0';
    signal ce: std_logic := '0';
    signal nwr: std_logic := '1';
    signal nrd: std_logic := '1';
    signal db: std_logic_vector(15 downto 0);
    signal cpu_data: std_logic_vector(15 downto 0) := (others => 'Z');
    signal adc_a: std_logic_vector(7 downto 0) := X"80";
    signal adc_b: std_logic_vector(7 downto 0) := X"00";
    signal sleep,clka,clkb: std_logic;
    constant data_minus: std_logic_vector(0 to 50) :=
        "000000000000101010111001001110100101001110010000000";
    constant data_plus: std_logic_vector(0 to 50) :=
        "111111111111010101000110110001011010110001100011111";
begin
    db <= cpu_data;
    dut: entity work.fpga_top
        generic map (fifo_depth_g => 256)
        port map (clk,rst_n,ce,nwr,nrd,db,sleep,clka,clkb,adc_a,adc_b);
    process
        procedure clock(n: natural := 1) is
        begin
            for i in 1 to n loop
                clk <= '0'; wait for 6944 ps;
                clk <= '1'; wait for 6944 ps;
            end loop;
        end procedure;
        procedure config(v: natural) is
        begin
            ce <= '1';cpu_data <= std_logic_vector(to_unsigned(v,16));nwr <= '0';
            clock(4);nwr <= '1';clock(4);ce <= '0';cpu_data <= (others => 'Z');clock(4);
        end procedure;
        procedure read_bus(expected: natural) is
        begin
            ce <= '1';nrd <= '0';clock(4);
            assert db = std_logic_vector(to_unsigned(expected,16)) report "MCU bus data mismatch" severity failure;
            nrd <= '1';clock(4);ce <= '0';clock(2);
            assert db = (db'range => 'Z') report "FPGA bus must be released" severity failure;
        end procedure;
        variable received: std_logic_vector(15 downto 0);
    begin
        clock(4);rst_n <= '1';clock(4);
        assert sleep='1' and clka=clk and clkb=clk report "ADC outputs wrong" severity failure;
        config(16#20#);read_bus(16#5542#);read_bus(16#5542#);
        config(16#4000# + 127*128); -- Accept all device addresses
        config(2); -- Enable USB, all filters disabled
        clock(1000);
        for i in data_plus'range loop
            adc_a(7) <= not data_minus(i);adc_b(7) <= not data_plus(i);
            clock(6);
        end loop;
        clock(80);
        config(3);read_bus(8);read_bus(8); -- Count reads must not advance FIFO
        config(16#22#);read_bus(16#5542#); -- Identity must not advance FIFO either
        config(3);read_bus(8);
        config(2);read_bus(16#A5#);read_bus(16#11#);read_bus(16#5A#);
        for i in 1 to 4 loop
            ce <= '1';nrd <= '0';clock(4);received := db;
            assert received(15 downto 8)=X"00" report "Timestamp incorrectly tagged as EOP" severity failure;
            nrd <= '1';clock(4);ce <= '0';clock(2);
        end loop;
        read_bus(16#100#);config(3);read_bus(0);
        rst_n <= '0';clock(3);rst_n <= '1';clock(3);config(1);read_bus(0);
        config(16#20#);read_bus(16#5542#);
        report "PASS USB top: identity, ADC polarity, packet bytes/EOP, count, FIFO pop, reset and bus release" severity note;
        wait;
    end process;
end architecture;
