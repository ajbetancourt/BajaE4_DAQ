#include <Arduino.h>
//*********************************************************************
// This code has been written by Robert Fraczkiewicz in December 2018 for
// the frequency and duty cycle meter of 3.3 V square waves based on timing 
// of their pulse width and period. The timing of both is measured by counting 
// the numberof internal 48 MHz clock cycles of the Atmel SAMD21 microcontroller 
// installedin Adafruit's ItsyBitsy M0 Express board. (https://www.adafruit.com/product/3727).
// It may or may not work with other MCUs featuring the ARM Cortex M0 processor.
// 
// This project's hardware and further details are described in the following Instructable:
// https://www.instructables.com/id/How-to-Measure-High-Frequency-and-Duty-Cycle-Simul/
// 
// To implement it on your project, you have to pick the output stream first. If you have 
// Adafruit's monochrome 128x32 OLED SPI display (https://www.adafruit.com/product/661) 
// connected to your ItsyBitsy M0 Express, as described in the Instructable, then leave 
// the OLED_DISPLAY_OUT uncommented. If you want to output measured data into the
// serial stream for debugging purposes, then uncomment SERIAL_OUT, keeping in
// mind that this extra communication will put additional strain on the CPU.
//
// And finally, you are welcome to use this code in your projects free of any monetary
// charge, but please do mention its source when posting results of your work.
// 
#define SERIAL_OUT

// Defines for input and control
#define signalPin 3 // Signal input
// WARNING: originalInputPin and dividedInputPin 4 must be negations of each other!
#define My_EXT_INTERRUPT  EIC_EVCTRL_EXTINTEO9
#define My_EIC_CONFIGn  1
#define My_EIC_SENSEn EIC_CONFIG_SENSE1_HIGH
#define My_EVGEN_INTERRUPT  EVSYS_ID_GEN_EIC_EXTINT_9

// Globals
volatile boolean periodComplete;
volatile boolean pulseComplete;
volatile uint16_t isrPeriod;
volatile uint16_t isrPulsewidth;
volatile boolean isError;
volatile boolean isOverflow;
uint16_t period;
uint16_t pulsewidth;
boolean error,overflow,divided;
uint16_t prescaler;
unsigned long countPeriod,countPulse;
double freq,duty;
unsigned long timeElapsed;
byte crank;

void stopTimer();
void startTimer();
void changePrescaler(uint16_t &prescaler, bool directn);
void InitializeTimerTC3();
void callBack(){};

void setup() 
{ 
#ifdef SERIAL_OUT
  // Initialize serial stream
  Serial.begin(115200);                  // Send data back on the Zero's native port
  while(!Serial);                        // Wait for the Serial port to be ready
  Serial.println("Starting");
#endif
  InitializeTimerTC3();

  // Initialize globals
  error=overflow=divided=false;
  countPeriod=countPulse=0;
  freq=duty=0.0;
  timeElapsed=millis();
  crank=0;
  delay(2000); // Pause for 2 seconds
}

void loop() 
{ 
  char activity_indicator[4]={'\\','|','/','-'};
  if(periodComplete)                    // Check if the period is complete
  {
    noInterrupts();                     // Read the new period
    period = isrPeriod;                   
    error = isError;
    overflow = isOverflow;
    if(overflow) { // period over 2^16-1
      // Low frequencies - divide internal clock
      
      changePrescaler(prescaler,true);
    } else if(period<16000)
      {
      // Intermediate frequencies
      if(prescaler>0) changePrescaler(prescaler,false);
      }
    if(pulseComplete)                     // Check if the pulse width is complete
    {
      pulsewidth = isrPulsewidth;
      if(divided) pulsewidth = 0.5*period - pulsewidth; // True pulse width at higher frequencies, by construction
      if(!error) {
        ++countPulse;
        duty+=static_cast<double>(pulsewidth);
      }
    }
    interrupts();
    periodComplete = false;                       // Start a new period
    pulseComplete = false;                        // Start a new pulse
    if(!error) {
      ++countPeriod;
      double temp=static_cast<double>(period);
      if(divided) temp/=4.0;
      freq+=temp;
    }
  }
  if(millis()-timeElapsed>=200)
  {// && countPeriod>0 && countPulse>0) {
    //stopTimer();
    freq/=countPeriod;
    duty/=countPulse;
    duty/=freq;
    duty*=100.0;
    switch(prescaler) {
    case TC_CTRLA_PRESCALER_DIV2:
      freq=24000.0/freq;
      break;
    case TC_CTRLA_PRESCALER_DIV4:
      freq=12000.0/freq;
      break;
    case TC_CTRLA_PRESCALER_DIV8:
      freq=6000.0/freq;
      break;
    case TC_CTRLA_PRESCALER_DIV16:
      freq=3000.0/freq;
      break;
    case TC_CTRLA_PRESCALER_DIV64:
      freq=750.0/freq;
      break;
    case TC_CTRLA_PRESCALER_DIV256:
      freq=187.5/freq;
      break;
    case TC_CTRLA_PRESCALER_DIV1024:
      freq=46.875/freq;
      break;
    default:
      freq=48000.0/freq;
      break;
    }
    #ifdef SERIAL_OUT
      Serial.print(period);
      Serial.print(F("\t"));
      Serial.print(pulsewidth);
      Serial.print(F("\t"));
      Serial.print(countPeriod);
      Serial.print(F("\t"));
      Serial.print(countPulse);
      Serial.print(F("\t"));
      if(freq<1.0) {
        Serial.print(1000*freq);
        Serial.print(F("Hz\t"));
      } else {
        Serial.print(freq);
        Serial.print(F("kHz\t"));
      }
      Serial.print(duty);
      Serial.print(F("%\t"));
      Serial.print(overflow);
      Serial.print(F("\t"));
      Serial.print(prescaler >> TC_CTRLA_PRESCALER_Pos,HEX);
      Serial.print(F("\t"));
      Serial.println(divided);
    #endif
    countPeriod=countPulse=0;
    freq=duty=0.0;
    timeElapsed=millis();
    //startTimer();
  } else {
    if(millis()<timeElapsed) timeElapsed=millis(); // Unlikely, but possible if running > 50 days...
  }
}


// up: directn = true, down: directn = false
void changePrescaler(uint16_t &prescaler, bool directn)
{
  uint16_t new_prescaler;
  TC3->COUNT16.CTRLA.bit.ENABLE=0;               // Disable TC3
  while (TC3->COUNT16.STATUS.bit.SYNCBUSY);       // Wait for synchronization
  prescaler = REG_TC3_CTRLA & TC_CTRLA_PRESCALER_Msk; // Current prescaler
  new_prescaler = prescaler >> TC_CTRLA_PRESCALER_Pos;
  if(directn) {
  // Up - divide clock by another 2
      if(new_prescaler<0x7ul) {
        REG_TC3_CTRLA ^= prescaler; // Clear current prescaler from CTRLA
        ++new_prescaler;
        prescaler = new_prescaler << TC_CTRLA_PRESCALER_Pos;
        REG_TC3_CTRLA |= prescaler;     // Set prescaler 
      }
  } else {
  // Down - multiply clock by another 2
      if(new_prescaler>=0x1ul) {
        REG_TC3_CTRLA ^= prescaler; // Clear current prescaler from CTRLA
        --new_prescaler;
        prescaler = new_prescaler << TC_CTRLA_PRESCALER_Pos;
        REG_TC3_CTRLA |= prescaler;     // Set prescaler 
      }
  }
  REG_TC3_CTRLA |= TC_CTRLA_PRESCSYNC_RESYNC |    // Reload or reset the counter on next generic clock. Reset the prescaler counter
                   TC_CTRLA_ENABLE;               // Enable TC3
  while (TC3->COUNT16.STATUS.bit.SYNCBUSY);       // Wait for synchronization
}

// The following two routines for servicing the signalPin interrupts is a modified version of several
// codes posted by Arduino Forum users electro_95, MartinL, and Rucus; here:
// https://forum.arduino.cc/index.php?topic=396804.5
void InitializeTimerTC3()
{
  REG_PM_APBCMASK |= PM_APBCMASK_EVSYS;     // Switch on the event system peripheral

  REG_GCLK_GENDIV = GCLK_GENDIV_DIV(1) |    // Divide the 48MHz system clock by 1 = 48MHz
                    GCLK_GENDIV_ID(5);      // Set division on Generic Clock Generator (GCLK) 5
  while (GCLK->STATUS.bit.SYNCBUSY);        // Wait for synchronization

  REG_GCLK_GENCTRL = GCLK_GENCTRL_IDC |           // Set the duty cycle to 50/50 HIGH/LOW
                     GCLK_GENCTRL_GENEN |         // Enable GCLK 5
                     GCLK_GENCTRL_SRC_DFLL48M |   // Set the clock source to 48MHz 
                     GCLK_GENCTRL_ID(5);          // Set clock source on GCLK 5
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization*/

  REG_GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN |         // Enable the generic clock...
                     GCLK_CLKCTRL_GEN_GCLK5 |     // ....on GCLK5
                     GCLK_CLKCTRL_ID_TCC2_TC3;    // Feed the GCLK5 to TCC2 and TC3
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

  // // Enable the port multiplexer on digital pin signalPin
  PORT->Group[g_APinDescription[signalPin].ulPort].PINCFG[g_APinDescription[signalPin].ulPin].bit.PMUXEN = 1;
  // Set-up the pin as an EIC (interrupt) peripheral on signalPin
  PORT->Group[g_APinDescription[signalPin].ulPort].PMUX[g_APinDescription[signalPin].ulPin >> 1].reg |= PORT_PMUX_PMUXO_A;
  //attachInterrupt(12,callBack,RISING);

  REG_EIC_EVCTRL |= My_EXT_INTERRUPT;                                      // Enable event output on external interrupt defined above
  EIC->CONFIG[My_EIC_CONFIGn].reg |= My_EIC_SENSEn;                        // Set event detecting a HIGH level
  EIC->CTRL.reg |= EIC_CTRL_ENABLE;                                        // Enable EIC peripheral
  while (EIC->STATUS.bit.SYNCBUSY);                                        // Wait for synchronization
  
  REG_EVSYS_USER = EVSYS_USER_CHANNEL(1) |                                // Attach the event user (receiver) to channel 0 (n + 1)
                   EVSYS_USER_USER(EVSYS_ID_USER_TC3_EVU);                // Set the event user (receiver) as timer TC3

  REG_EVSYS_CHANNEL = EVSYS_CHANNEL_EDGSEL_NO_EVT_OUTPUT |                // No event edge detection
                      EVSYS_CHANNEL_PATH_ASYNCHRONOUS |                   // Set event path as asynchronous
                      EVSYS_CHANNEL_EVGEN(My_EVGEN_INTERRUPT) |           // Set event generator (sender) as external interrupt defined above
                      EVSYS_CHANNEL_CHANNEL(0);                           // Attach the generator (sender) to channel 0

  REG_TC3_EVCTRL |= TC_EVCTRL_TCEI |              // Enable the TC event input
                    /*TC_EVCTRL_TCINV |*/         // Invert the event input
                    TC_EVCTRL_EVACT_PPW;          // Set up the timer for capture: CC0 period, CC1 pulsewidth
                   
  REG_TC3_READREQ = TC_READREQ_RREQ |             // Enable a read request
                    TC_READREQ_ADDR(0x06);        // Offset of the CTRLC register
  while (TC3->COUNT16.STATUS.bit.SYNCBUSY);       // Wait for (read) synchronization
 
  REG_TC3_CTRLC |= TC_CTRLC_CPTEN1 |              // Enable capture on CC1
                   TC_CTRLC_CPTEN0;               // Enable capture on CC0
  while (TC3->COUNT16.STATUS.bit.SYNCBUSY);       // Wait for (write) synchronization

  //NVIC_DisableIRQ(TC3_IRQn);
  //NVIC_ClearPendingIRQ(TC3_IRQn);
  NVIC_SetPriority(TC3_IRQn, 0);      // Set the Nested Vector Interrupt Controller (NVIC) priority for TC3 to 0 (highest)
  NVIC_EnableIRQ(TC3_IRQn);           // Connect the TC3 timer to the Nested Vector Interrupt Controller (NVIC)
 
  REG_TC3_INTENSET = TC_INTENSET_MC1 |            // Enable compare channel 1 (CC1) interrupts
                     TC_INTENSET_MC0;             // Enable compare channel 0 (CC0) interrupts
  
  REG_TC3_CTRLA |= TC_CTRLA_PRESCALER_DIV1 |      // Set prescaler to 16, 16MHz/16 = 1MHz
                   TC_CTRLA_ENABLE;               // Enable TC3
  while (TC3->COUNT16.STATUS.bit.SYNCBUSY);       // Wait for synchronization
  prescaler=TC_CTRLA_PRESCALER_DIV1;              // Store the starting prescaler
  
  delay(1000);  
}

void TC3_Handler()                                // Interrupt Service Routine (ISR) for timer TC3
{     
  // Check for match counter 0 (MC0) interrupt
  if (TC3->COUNT16.INTFLAG.bit.MC0)             
  {
    REG_TC3_READREQ = TC_READREQ_RREQ |           // Enable a read request
                      TC_READREQ_ADDR(0x18);      // Offset address of the CC0 register
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY);     // Wait for (read) synchronization
    isrPeriod = REG_TC3_COUNT16_CC0;              // Copy the period  
    isError=TC3->COUNT16.INTFLAG.bit.ERR;
    isOverflow=TC3->COUNT16.INTFLAG.bit.OVF;
    TC3->COUNT16.INTFLAG.bit.ERR=0;
    TC3->COUNT16.INTFLAG.bit.OVF=0;
    periodComplete = true;                         // Indicate that the period is complete
  }

  // Check for match counter 1 (MC1) interrupt
  if (TC3->COUNT16.INTFLAG.bit.MC1)           
  {
    REG_TC3_READREQ = TC_READREQ_RREQ |           // Enable a read request
                      TC_READREQ_ADDR(0x1A);      // Offset address of the CC1 register
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY);     // Wait for (read) synchronization
    isrPulsewidth = REG_TC3_COUNT16_CC1;          // Copy the pulse-width
    pulseComplete = true;                         // Indicate that the pulse width is complete
  }
}

void stopTimer()
{
  detachInterrupt(signalPin);
  REG_TC3_CTRLBSET = TC_CTRLBSET_CMD_STOP;
  while (TC3->COUNT16.STATUS.bit.SYNCBUSY); 
}

void startTimer()
{
  attachInterrupt(signalPin,callBack,RISING);
  REG_TC3_CTRLBSET = TC_CTRLBSET_CMD_RETRIGGER;
  while (TC3->COUNT16.STATUS.bit.SYNCBUSY); 
}

