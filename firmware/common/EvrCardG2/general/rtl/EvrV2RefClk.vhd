-------------------------------------------------------------------------------
-- Title      : 
-------------------------------------------------------------------------------
-- File       : EvrV2RefClk.vhd
-- Author     : Matt Weaver <weaver@slac.stanford.edu>
-- Company    : SLAC National Accelerator Laboratory
-- Created    : 2022-04-01
-- Last update: 2021-09-16
-- Platform   : 
-- Standard   : VHDL'93/02
-------------------------------------------------------------------------------
-- Description: 
-------------------------------------------------------------------------------
-- This file is part of 'LCLS2 Timing Core'.
-- It is subject to the license terms in the LICENSE.txt file found in the 
-- top-level directory of this distribution and at: 
--    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html. 
-- No part of 'LCLS2 Timing Core', including this file, 
-- may be copied, modified, propagated, or distributed except according to 
-- the terms contained in the LICENSE.txt file.
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;
use ieee.NUMERIC_STD.all;


library surf;
use surf.StdRtlPkg.all;

entity EvrV2RefClk is
  generic (
    TPD_G          : time             := 1 ns );
  port (
    -- EVR Ports
    evrClk              : in  sl;
    evrRst              : in  sl;
    evrClkSel           : in  sl;
    refClkOut           : out sl );
end EvrV2RefClk;

architecture mapping of EvrV2RefClk is

  type RegType is record
    clk : sl;
  end record;

  constant REG_INIT_C : RegType := (
    clk <= '0' );

  signal r   : RegType := REG_INIT_C;
  signal rin : RegType;
  
  signal clk70, clko_0, clko_1, clko : sl;

  constant USE_REG_C : boolean := false; -- make 20 MHz clk and register the
                                         -- output OR make 10 MHz clk
  
begin  -- rtl

  clko <= clko_0 when evrClkSel = '0' else clko_1;
  
  U_CLK186 : entity surf.ClockManager7
    generic map ( INPUT_BUFG_G     => false,
                  NUM_CLOCKS_G     => 1,
                  CLKIN_PERIOD_G   => 5.4,
                  CLKFBOUT_MULT_G  => 7,
                  DIVCLK_DIVIDE_G  => 13,
                  CLKOUT0_DIVIDE_G => ite(USE_REG_C, 5, 10) )
    port map ( clkIn  => evrClk,
               clkOut(0) => clko_1 );
  
  GEN_20MH : if USE_REG_C generate
    --  Generate 20 MHz clocks and register flop on each edge
    U_CLK119 : entity surf.ClockManager7
      generic map ( INPUT_BUFG_G     => false,
                    NUM_CLOCKS_G     => 1,
                    CLKIN_PERIOD_G   => 8.4,
                    CLKFBOUT_MULT_G  => 10,
                    DIVCLK_DIVIDE_G  => 17,
                    CLKOUT0_DIVIDE_G => 1 )
      port map ( clkIn  => evrClk,
                 clkOut(0) => clk70 );

    U_CLK70 : entity surf.ClockManager7
      generic map ( INPUT_BUFG_G     => false,
                    NUM_CLOCKS_G     => 1,
                    CLKIN_PERIOD_G   => 14.3,
                    CLKFBOUT_MULT_G  => 20,
                    DIVCLK_DIVIDE_G  => 14,
                    CLKOUT0_DIVIDE_G => 5 )
      port map ( clkIn  => clk70,
                 clkOut(0) => clko_0 );

    refClkOut <= r.clk;

    comb : process(r, evrRst) is
      variable v : RegType;
    begin
      v := r;
      v.clk := not r.clk;

      if evrRst = '1' then
        v := REG_INIT_C;
      end if;
      
      rin <= v;
    end process comb;
    
    seq: process (clko) is
    begin
      if rising_edge(clko) then
        r <= rin;
      end if;
    end process seq;
    
  end generate;
  
  NOGEN_REG : if not USE_REG_C generate
    --  Generate 10MHz clocks.  Done.
    refClkOut <= clko;

    U_CLK119 : entity surf.ClockManager7
      generic map ( INPUT_BUFG_G     => false,
                    NUM_CLOCKS_G     => 1,
                    CLKIN_PERIOD_G   => 8.4,
                    CLKFBOUT_MULT_G  => 10,
                    DIVCLK_DIVIDE_G  => 17,
                    CLKOUT0_DIVIDE_G => 7 )
      port map ( clkIn  => evrClk,
                 clkOut(0) => clko_0 );
  end generate;
  
end mapping;