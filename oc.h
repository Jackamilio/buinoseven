/*  TestOverclock: a test sketch to overclock the ATSAMD21
 *  Copyright 2020 Nicola Wrachien www.next-hack.com 
 * 
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.   
 * 
 * 
 */

// CONFIGURATION DEFINES
#define DESIRED_MHZ 70UL      // desired overclock frequency in MHz
#define REF_CLK 48000000    // frequency of the reference oscillator. 48MHz is for the DFLL48.
#define OVERCLOCK_GCLK_ID 4   // gen clock used as reference for the DPLL
// Utility defines
#define FLASH_WAIT_STATES 2 //((DESIRED_MHZ + 23UL) / 24UL - 1UL)
#define GCLK_DIVIDE_FACTOR (REF_CLK / 1000000)
#define   CLOCK_PLL_MUL   (DESIRED_MHZ - 1)
#define   CLOCK_PLL_DIV   (1U) 
// These defines are required for the DPLL configuration.
#define   GCLK_GENCTRL_SRC_DPLL96M_Val    (0x8)   /**< \brief (GCLK_GENCTRL) DPLL96M output */
#define GCLK_GENCTRL_SRC_Pos        8            /**< \brief (GCLK_GENCTRL) Source Select */
#define GCLK_GENCTRL_SRC_DPLL96M    (GCLK_GENCTRL_SRC_DPLL96M_Val  << GCLK_GENCTRL_SRC_Pos)
//
void oc( void ) 
{

  // detach the USB to ensure that no issue will occur when we change its frequency generator (Arduino by default uses GCLK0).
//  USBDevice.detach();

  // Set the number flash wait states to FLASH_WAIT_STATES.
  NVMCTRL->CTRLB.bit.RWS = FLASH_WAIT_STATES;
  // setup a generic clock to feed DPLL. This clock in turn is fed by the DFLL @48MHz divided by 48 (1MHz) 
  // divide the clock so that the DPLL is fed at 1MHz 
  GCLK->GENDIV.reg = (GCLK_GENDIV_DIV(GCLK_DIVIDE_FACTOR) |   GCLK_GENDIV_ID(OVERCLOCK_GCLK_ID));
  while ( GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY );      // Synchronize
  //  Set DFLL48M as source of OVERCLOCK_GCLK 
  GCLK->GENCTRL.reg = GCLK_GENCTRL_GENEN |  GCLK_GENCTRL_SRC_DFLL48M | GCLK_GENCTRL_ID(OVERCLOCK_GCLK_ID); 
  while ( GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY );      // Synchronize
  // set the OVERCLOCK_GCLK as reference for the DPLL
  GCLK->CLKCTRL.reg = (GCLK_CLKCTRL_GEN(OVERCLOCK_GCLK_ID) | GCLK_CLKCTRL_ID(GCLK_CLKCTRL_ID_FDPLL_Val) | GCLK_CLKCTRL_CLKEN);
  while ( GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY );      // Synchronize
  // enable the DPLL
  SYSCTRL->DPLLRATIO.reg = (SYSCTRL_DPLLRATIO_LDR(CLOCK_PLL_MUL));
  SYSCTRL->DPLLCTRLB.reg = (SYSCTRL_DPLLCTRLB_REFCLK_GCLK) | SYSCTRL_DPLLCTRLB_FILTER(SYSCTRL_DPLLCTRLB_FILTER_DEFAULT_Val); 
  SYSCTRL->DPLLCTRLA.reg = (SYSCTRL_DPLLCTRLA_ENABLE);
  while(!(SYSCTRL->DPLLSTATUS.reg & (SYSCTRL_DPLLSTATUS_CLKRDY | SYSCTRL_DPLLSTATUS_LOCK))); // Synchronize
  //
  // select the DPLL as source for clock generator 0 (CPU core clock)
  GCLK->GENDIV.reg =  (GCLK_GENDIV_DIV(CLOCK_PLL_DIV) | GCLK_GENDIV_ID(0));
  while ( GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY ); // Synchronize
  GCLK->GENCTRL.reg = (GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_DPLL96M | GCLK_GENCTRL_ID(0));
  while (GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY); // Synchronize

  // Now, the USB must be clocked at 48 MHZ. The Arduino USB library sets the USB clock by feedfing if from GCLK0, instead of directly by the DFLL48MHz
  // However, by overclocking GCLK0, the USB will not work, as it would run at a different frequency. Here we set the GCLK6 to the DFLL 48MHz
  // To avoid issues, first we disable the clock.
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(6)     | // Generic Clock Multiplexer 6 (USB)
                     GCLK_CLKCTRL_GEN_GCLK0;
  while (GCLK->CLKCTRL.bit.CLKEN);  // Wait until we have actually disabled the clock 
  // now configure clock 5 so that it is fed by the DFLL48MHz
  GCLK->GENDIV.reg = GCLK_GENDIV_ID( 5 ) ; // Generic Clock Generator 5
  while ( GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY ); // Synchronize
  GCLK->GENCTRL.reg = GCLK_GENCTRL_ID( 5 ) | // Generic Clock Generator 0
                      GCLK_GENCTRL_SRC_DFLL48M | // Selected source is DFLL 48MHz
                      GCLK_GENCTRL_IDC | // Set 50/50 duty cycle
                      GCLK_GENCTRL_GENEN ;
  while ( GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY ); // Synchronize
  // Now we can change the input
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(6)     | // Generic Clock Multiplexer 6 (USB)
                      GCLK_CLKCTRL_GEN_GCLK5;
  while ( GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY ); // Synchronize
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(6)     | // Generic Clock Multiplexer 6 (USB)
                      GCLK_CLKCTRL_GEN_GCLK5 | // USE CLOCK 5 for USB
                      GCLK_CLKCTRL_CLKEN;
  while ( GCLK->STATUS.reg & GCLK_STATUS_SYNCBUSY ); // Synchronize
  // reattach the USB
//  USBDevice.attach();

  // Set BuiltIn LED GPIO as output.
//  REG_PORT_DIRSET0 = 1 << GPIO_NUMBER;
}
