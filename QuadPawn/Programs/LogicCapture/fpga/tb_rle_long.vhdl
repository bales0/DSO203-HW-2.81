library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity tb_rle_long is end entity;
architecture testbench of tb_rle_long is
    signal clk,rst_n,write: std_logic := '0';
    signal din: std_logic_vector(3 downto 0) := "0000";
    signal dout: std_logic_vector(15 downto 0);
begin
    -- Exercise the same cutoff/output logic at reduced counter width. The
    -- hardware top leaves the default 26-bit counter unchanged.
    dut: entity work.RLECoder generic map(time_width_g=>13) port map(clk,rst_n,din,dout,write);
    process
        variable ticks,total,markers,event_time: natural := 0;
        procedure clock is
        begin
            clk<='0';wait for 5 ns;clk<='1';wait for 5 ns;ticks:=ticks+1;
            if write='1' then
                if dout(15)='1' then
                    markers:=markers+1;
                    total:=total+to_integer(unsigned(dout(14 downto 0)))*2048;
                else
                    total:=total+to_integer(unsigned(dout(14 downto 4)))+1;
                end if;
            end if;
        end procedure;
    begin
        clock;rst_n<='1';ticks:=0;
        -- Three complete periodic time segments, then a final level change.
        for i in 1 to 18442 loop clock;end loop;
        din<="0001";clock;event_time:=ticks;clock;
        assert markers=3 report "Long idle interval marker was lost" severity failure;
        assert total=event_time report "Long interval elapsed time is wrong" severity failure;
        assert dout(3 downto 0)="0001" report "Final levels wrong" severity failure;
        report "PASS RLE long idle: periodic marker and exact elapsed cycles" severity note;
        wait;
    end process;
end architecture;
