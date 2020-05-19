library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

-- Uncomment the following library declaration if using
-- arithmetic functions with Signed or Unsigned values
use IEEE.NUMERIC_STD.ALL;

-- Uncomment the following library declaration if instantiating
-- any Xilinx leaf cells in this code.
--library UNISIM;
--use UNISIM.VComponents.all;

-- simulation testbench
-- instantiates and connects the top component to be simulated
entity tb_axis_ip is
--  Port ( );
end tb_axis_ip;

architecture Behavioral of tb_axis_ip is

COMPONENT axis_macc_0
  PORT (
    ap_clk : IN STD_LOGIC;
    ap_rst_n : IN STD_LOGIC;
    strm_out_TVALID : OUT STD_LOGIC;
    strm_out_TREADY : IN STD_LOGIC;
    strm_out_TDATA : OUT STD_LOGIC_VECTOR(31 DOWNTO 0);
    strm_out_TLAST : OUT STD_LOGIC_VECTOR(0 DOWNTO 0);
    strm_in_TVALID : IN STD_LOGIC;
    strm_in_TREADY : OUT STD_LOGIC;
    strm_in_TDATA : IN STD_LOGIC_VECTOR(31 DOWNTO 0);
    strm_in_TLAST : IN STD_LOGIC_VECTOR(0 DOWNTO 0)
  );
END COMPONENT;

--Inputs
      signal clk : std_logic := '0';
      signal rstn : std_logic := '0';
  
      signal S_AXIS_TDATA : std_logic_vector(31 downto 0) := (others => '0');
      signal S_AXIS_TLAST : std_logic_vector(0 downto 0) := "0";
      signal S_AXIS_TVALID : std_logic := '0';
      signal M_AXIS_TREADY : std_logic := '0';
      
      --Outputs
      signal M_AXIS_TDATA : std_logic_vector(31 downto 0);
      signal M_AXIS_TLAST : std_logic_vector(0 downto 0) := "0";
      signal M_AXIS_TVALID : std_logic;
      signal S_AXIS_TREADY : std_logic;
      
      -- Clock period definitions
      constant clk_period : time := 10 ns;
      constant t_hdelay : time := 2 ns;
      
    signal selB : std_logic := '0';
    signal M_transaction : std_logic := '0';
    signal idataA : integer := 0;
    signal idataB : integer := 0;
    
    type kernel_array is array (0 to 3) of integer;
    signal kernel : kernel_array := (0, 0, 1, 0);
    type imag_array is array (0 to 15) of integer;
    signal imag : imag_array := (-1, -10, 20, -20, -42, -13, -4, -25, -42, -28, 10, -9, -28, -6, -5, -29);
    signal result : integer := 0;
    
  begin
      -- Instantiate the Unit Under Test (UUT)
    uut : axis_macc_0
      PORT MAP (
        ap_clk => clk, ap_rst_n => rstn,
        strm_out_TVALID => M_AXIS_TVALID,
        strm_out_TREADY => M_AXIS_TREADY,
        strm_out_TDATA => M_AXIS_TDATA,
        strm_out_TLAST => M_AXIS_TLAST,
        strm_in_TVALID => S_AXIS_TVALID,
        strm_in_TREADY => S_AXIS_TREADY,
        strm_in_TDATA => S_AXIS_TDATA,
        strm_in_TLAST => S_AXIS_TLAST
    );
      
      -- Clock definition
      clk <= not clk after clk_period/2;
   
      S_AXIS_TDATA <= std_logic_vector( to_signed(idataB, 32)) when selB='1' else
                      std_logic_vector( to_signed(idataA, 32));
      M_AXIS_TREADY <= '1' after 200 ns;
      -- M_AXIS_TREADY <= not M_AXIS_TREADY after 13*clk_period;
      
      M_transaction <= M_AXIS_TREADY and M_AXIS_TVALID; 
       -- Stimulus process
             stim_proc: process
       begin        
         -- hold reset state for 100 ns.
         wait for 100 ns;    
         rstn <= '1';
         -- insert stimulus here 
         wait for 10*clk_period; 
         wait until rising_edge(clk); wait for t_hdelay; -- to synchronize input variation with positive clock edge + hdelay 
         
         for i in 0 to 3 loop -- loop to send kernel matrix
            selB <= '1';
            idataB <= kernel(i);
            
            if i = 3 then
                S_AXIS_TLAST <= "1";
            else
                S_AXIS_TLAST <= "0";
            end if;
            S_AXIS_TVALID <= '1';
            
            if S_AXIS_TREADY /= '1' then
               wait until S_AXIS_TREADY='1';
            end if;
            wait until rising_edge(clk); 
            wait for t_hdelay;

            S_AXIS_TVALID <= '0';
            S_AXIS_TLAST <= "0";
         end loop; -- finished sending kernel matrix
         
         wait for clk_period; 
         wait until rising_edge(clk); 
         wait for t_hdelay+clk_period*10;
         
         for i in 0 to 15 loop -- loop to send image matrix
            
            selB <= '0';
            idataA <= imag(i);
            
            if i = 15 then
                S_AXIS_TLAST <= "1";
            else
                S_AXIS_TLAST <= "0";
            end if;
            S_AXIS_TVALID <= '1';
            
            if S_AXIS_TREADY /= '1' then
               wait until S_AXIS_TREADY='1';
            end if;
            wait until rising_edge(clk); 
            wait for t_hdelay;

            S_AXIS_TVALID <= '0';
            S_AXIS_TLAST <= "0";
         end loop; -- finished sending image
    
         wait;
       end process;
   
       r_proc: process
       begin
         for i in 0 to 9 loop  -- reading result matrix
             if M_transaction /= '1' then
               wait until M_transaction='1';
             end if;
    
             result <= 0; wait for 1 ps;
             for k in 0 to 3 loop 
               result <= result + kernel(k) * imag(i*4+k);
               wait for 1 ps;
             end loop; -- k
             wait until rising_edge(clk);
             wait for t_hdelay; 
         end loop;  -- i
       end process;   
       
  end Behavioral;
