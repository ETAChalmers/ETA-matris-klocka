/* Written by Sten for ETA clock during 2018
 * Clock uses avalible CAN time and displays on large LED MATRIX display. 
 */
// PORTx set output value here if conf as output
// DDRx configure input/output, 0 = input, 1 = output
// PINx port input register, read here if conf as input

#include <mcp_can.h>
#include <mcp_can_dfs.h>
#include <SPI.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <time.h>


#define SHIFT_REGISTER DDRB
#define LATCH_REGISTER DDRC
#define SHIFT_PORT PORTB
//#define LATCH_PORT PORTD
#define LATCH_PORT PORTC
#define DATA (1<<PB3)           //MOSI (SI)
//#define LATCH (1<<PD7)          //
#define LATCH (1<<PC5)
#define CLOCK (1<<PB5)          //SCK  (SCK)

#define DISPLAY_SIZE 24
#define CHARACTER_WIDTH 6
#define LINE_CHAR_WIDTH 6
#define COLON_CHAR_WIDTH 2
#define COFFE_CHAR_WIDTH 8
#define DOT_CHAR_WIDTH 2
#define LINE_CHAR 10
#define COLON_CHAR 11
#define DOT_CHAR 12
#define COFFE_CHAR 13


uint32_t local_unix_time_from_Can = 0;
uint32_t local_unix_time_to_display = 0;

uint32_t local_unix_time_buffer[3] = {0,0,0};
uint8_t time_buffer_index = 0;

unsigned long ticks = 0;
struct tm *clock_time;

// int tm_sec;         /* seconds,  range 0 to 59          */
// int tm_min;         /* minutes, range 0 to 59           */
// int tm_hour;        /* hours, range 0 to 23             */
// int tm_mday;        /* day of the month, range 1 to 31  */
// int tm_mon;         /* month, range 0 to 11             */
// int tm_year;        /* The number of years since 1900   */
// int tm_wday;        /* day of the week, range 0 to 6    */
// int tm_yday;        /* day in the year, range 0 to 365  */
// int tm_isdst;       /* daylight saving time             */

const char charTable[14][9] ={{0x70,0x88,0x88,0x88,0x88,0x88,0x70,0x00,CHARACTER_WIDTH},  // Pixelmap for characters on LED display 
                              {0x20,0x60,0x20,0x20,0x20,0x20,0x70,0x00,CHARACTER_WIDTH},
                              {0x70,0x88,0x08,0x10,0x20,0x40,0xf8,0x00,CHARACTER_WIDTH},
                              {0x70,0x88,0x08,0x10,0x08,0x88,0x70,0x00,CHARACTER_WIDTH},
                              {0x10,0x30,0x50,0x90,0xf8,0x10,0x38,0x00,CHARACTER_WIDTH},
                              {0xf8,0x80,0xf0,0x08,0x08,0x88,0x70,0x00,CHARACTER_WIDTH},
                              {0x30,0x40,0x80,0xf0,0x88,0x88,0x70,0x00,CHARACTER_WIDTH},
                              {0xf8,0x08,0x10,0x20,0x40,0x40,0x40,0x00,CHARACTER_WIDTH},
                              {0x70,0x88,0x88,0x70,0x88,0x88,0x70,0x00,CHARACTER_WIDTH},
                              {0x70,0x88,0x88,0x78,0x08,0x10,0x60,0x00,CHARACTER_WIDTH},
                              {0x00,0x00,0x00,0xf8,0x00,0x00,0x00,0x00,LINE_CHAR_WIDTH},
                              {0x00,0x80,0x80,0x00,0x80,0x80,0x00,0x00,COLON_CHAR_WIDTH},
                              {0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x80,DOT_CHAR_WIDTH},
                              {0x50,0x28,0x50,0xFF,0x85,0x86,0x84,0x78,COFFE_CHAR_WIDTH}};

char display_shadow[DISPLAY_SIZE/8+1][8]; //LED display shadow

long unsigned int rxId;
unsigned char len = 0;
unsigned char rxBuf[8];
char msgString[128];                        // Array to store serial string

#define CAN0_INT 2                              // Set INT to pin 2
MCP_CAN CAN0(8);                               // Set CS to pin 8

void setup()
{

    DDRB = 0b11111111; // all output, 
    
    //initialize SPI module and pins
    SHIFT_REGISTER |= (DATA | CLOCK);     //Set control pins as outputs
    LATCH_REGISTER = (LATCH);
    
    SHIFT_PORT &= ~(DATA | CLOCK);                //Set control pins low
    SPCR = (1<<SPE) | (1<<MSTR)| (1<<SPR0);  //Start SPI as Master
    SPSR =  (1<<SPI2X);

    //Pull LATCH low (Important: this is necessary to start the SPI transfer!)
    LATCH_PORT &= ~LATCH;
    
    
    wdt_reset();
    wdt_enable(WDTO_4S);
    WDTCSR |= (1<<WDCE);
    WDTCSR |= (1<<WDIE);
    WDTCSR &= ~(1<<WDE);
    wdt_reset();
    
    set_zone(-1 * ONE_HOUR); // set timezone (offset from GMT)
    set_dst(eu_dst); // specify daylight saving time function

    
    //set_position( 57.6881 * ONE_DEGREE, 11.9778 * ONE_DEGREE); // not used yet

    //Start serial com.
    Serial.begin(9600);

    //cli();          // disable global interrupts
    sei();          // enable global interrupts


     
    // Initialize MCP2515 running at 16MHz with a baudrate of 500kb/s and the masks and filters disabled.
    while(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) != CAN_OK)
    //if(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) == CAN_OK)
    {
      Serial.println("Error Initializing MCP2515..."); 
      print_error();
      update_display();  // send display shadow to LED screen
      delay(100);
    }
    Serial.println("MCP2515 Initialized Successfully!");
    CAN0.setMode(MCP_NORMAL);                     // Set operation mode to normal so the MCP2515 sends acks to received data.

    
    pinMode(CAN0_INT, INPUT);                            // Configuring pin for /INT input
    pinMode(6, OUTPUT);                            // Configuring pin for dim output
    analogWrite(6, 240);
  
}

void loop() {
  
   
  if(local_unix_time_from_Can != local_unix_time_buffer[time_buffer_index])
  {
    if(++time_buffer_index > 2)
      time_buffer_index = 0;

    local_unix_time_buffer[time_buffer_index] = local_unix_time_from_Can;

    Serial.print("Outlier test; ");
    Serial.print(local_unix_time_from_Can);
    Serial.print("; ");
    
    if( (local_unix_time_buffer[2] + local_unix_time_buffer[1] + local_unix_time_buffer[0] + 5) > 3*local_unix_time_from_Can && 3*local_unix_time_from_Can > (local_unix_time_buffer[2] + local_unix_time_buffer[1] + local_unix_time_buffer[0] - 5)  )
    {
      local_unix_time_to_display = local_unix_time_buffer[time_buffer_index];
    }

    Serial.println(local_unix_time_to_display);
  }


  if (local_unix_time_to_display != 0)
  {
    
    clock_time = localtime(&local_unix_time_to_display);
    print_time();
  } else {
    print_error();
    Serial.println("error");
  }
  //print_coffe();
  update_display();  // send display shadow to LED screen

  //print_display_shadow();
  //print_time_serial();

  delay(1);

  if(fetch_time())
  {
    Serial.println("WDT_RESET");
    wdt_reset();   
  }
  
}

void print_time_serial() //debug printing 
{
    unsigned short int clock_hh = (*clock_time).tm_hour;
    unsigned short int clock_mm = (*clock_time).tm_min;
    unsigned short int clock_ss = (*clock_time).tm_sec;
    
    Serial.print(clock_hh/10);
    Serial.print(clock_hh%10);
    Serial.print(":");
    Serial.print(clock_mm/10);
    Serial.print(clock_mm%10);
    Serial.print(":");
    Serial.print(clock_ss/10);
    Serial.println(clock_ss%10); 
}

void print_display_shadow() //debug printing
{
    for(unsigned short int i = 0; i < 8 ; i++)
    {
      Serial.print(i);
      Serial.print("  ");
      Serial.println((unsigned short int)display_shadow[0][i]);
    }
    Serial.println('*');
     
}

uint8_t fetch_time(){
    static uint32_t old_unix_time_from_Can = 0;
  
    if(!digitalRead(CAN0_INT))                         // If CAN0_INT pin is low, read receive buffer
    {
      CAN0.readMsgBuf(&rxId, &len, rxBuf);      // Read data: len = data length, buf = data byte(s)
               
      if((rxId & 0x80000000) == 0x80000000)     // Determine if ID is standard (11 bits) or extended (29 bits)
        sprintf(msgString, "Extended ID: 0x%.8lX  DLC: %1d  Data:", (rxId & 0x1FFFFFFF), len);
      else
        sprintf(msgString, "Standard ID: 0x%.3lX       DLC: %1d  Data:", rxId, len);
  
      Serial.print(msgString);
  
      if((rxId & 0x40000000) == 0x40000000){    // Determine if message is a remote request frame.
        sprintf(msgString, " REMOTE REQUEST FRAME");
        //Serial.print(msgString);
      } else {
        for(byte i = 0; i<len; i++){
          sprintf(msgString, " 0x%.2X", rxBuf[i]);
          Serial.print(msgString);
        }
      }                                                   
      //Serial.println();                                     

      if( ((rxId & 0x80000000) == 0x80000000) &&  (rxId & 0x1FFFFFFF) == 0x14002806 )
      {
        Serial.print("Coffe: ");
        sprintf(msgString, "Extended ID: 0x%.8lX  DLC: %1d  Data:", (rxId & 0x1FFFFFFF), len);
        Serial.print(msgString);
        for(byte i = 0; i<len; i++){
          sprintf(msgString, " 0x%.2X", rxBuf[i]);
          Serial.print(msgString);
        }
        Serial.println("");
      }

      if( ((rxId & 0x80000000) == 0x80000000) &&  (rxId & 0x1FFFFFFF) == 0x14002906 )
      {
        Serial.print("Coffe: ");
        sprintf(msgString, "Extended ID: 0x%.8lX  DLC: %1d  Data:", (rxId & 0x1FFFFFFF), len);
        Serial.print(msgString);
        for(byte i = 0; i<len; i++){
          sprintf(msgString, " 0x%.2X", rxBuf[i]);
          Serial.print(msgString);
        }
        Serial.println("");
      }

      if( ((rxId & 0x80000000) == 0x80000000) &&  (rxId & 0x1FFFFFFF) == 0 )
      {
        Serial.print("Timestamp: ");
        
        local_unix_time_from_Can = *((uint32_t*)rxBuf);
        Serial.println(local_unix_time_from_Can);

        if(old_unix_time_from_Can != local_unix_time_from_Can)
        {
          old_unix_time_from_Can = local_unix_time_from_Can;
          return 1;  
        }
      }
              
    }
    return 0;
}

void output_byte_spi(unsigned short data)
{
    //Shift in some data
    SPDR = data;
    //Wait for SPI process to finish
    while(!(SPSR & (1<<SPIF))) ;
    
    //Latch into output latch
    LATCH_PORT |= LATCH;
    delayMicroseconds(2);
    LATCH_PORT &= ~LATCH;
    delayMicroseconds(2);
}

void send_message_spi(char* data,unsigned short message_length)
{
    for(int i = 0; i < message_length ; i++) 
    {   
        //Shift in some data
        SPDR = data[i];
        //Wait for SPI process to finish
        while(!(SPSR & (1<<SPIF))) ;
    }
    
    //Latch into output latch
    LATCH_PORT |= LATCH;
    delayMicroseconds(2);
    LATCH_PORT &= ~LATCH;
    delayMicroseconds(2);

}

void print_time()
{
   unsigned short int clock_hh = (*clock_time).tm_hour;
   unsigned short int clock_mm = (*clock_time).tm_min;
   unsigned short int clock_ss = (*clock_time).tm_sec;
   unsigned short int col = 0;
   char charBuf[9];
      
   clear_all_display_shadow();   //Clear displayshodow memory

   col = write_character_to_display_shadow(charTable[clock_hh/10],col);
   //col = write_character_to_display_shadow(charTable[clock_hh%10],col);
   copyCharacter(charTable[clock_hh%10],charBuf);
   charBuf[7] = (clock_ss<<2) & 0b00000100 | ~(clock_ss<<1) & 0b00000010;
   col = write_character_to_display_shadow(charBuf,col);
   
   col += 1; // pixel columns in between hours and minutes
   
   col = write_character_to_display_shadow(charTable[clock_mm/10],col);
   
   //col = write_character_to_display_shadow(charTable[clock_mm%10],col); 
   copyCharacter(charTable[clock_mm%10],charBuf);
   charBuf[7] = 0; // fix shit
   col = write_character_to_display_shadow(charBuf,col);

}

void copyCharacter(char* data, char* buf)
{
  for(uint8_t i = 0; i < 9 ; i++)
  {
    buf[i] = data[i];
  }
}

void print_coffe()
{

   unsigned short int col = 3;
      
   clear_all_display_shadow();   //Clear displayshodow memory
   
   col = write_character_to_display_shadow(charTable[COFFE_CHAR],col);
   col += 2; // pixel columns in between coffe cups
   col = write_character_to_display_shadow(charTable[COFFE_CHAR],col); 
}

void print_error()
{
   unsigned short int col = 0;
      
   clear_all_display_shadow();   //Clear displayshodow memory
   
   col = write_character_to_display_shadow(charTable[LINE_CHAR],col);
   col = write_character_to_display_shadow(charTable[LINE_CHAR],col) + 1;
   col = write_character_to_display_shadow(charTable[LINE_CHAR],col);
   col = write_character_to_display_shadow(charTable[LINE_CHAR],col);  
}


unsigned short int write_character_to_display_shadow(char* data, unsigned short col)
{
  unsigned int tmp;
  unsigned short int i;
  //uint16_t tmp[8];
  
  for(i = 0; i < 8 ; i++) // for all rows, do pointer magic so it the character will be split between two bytes.  
  {
    *((char *)&tmp+1) = data[i];
    *((char *)&tmp) = 0;
    tmp = tmp>>(col%8);

    display_shadow[col/8][i] |= *(((char *)&tmp)+1);
    display_shadow[(col/8)+1][i] = *((char *)&tmp);  
  }
  return col+data[i]; // add the character width
}

void clear_rest_display_shadow(unsigned short col)
{
  for(unsigned short panel = col/8 + 1 ; panel < DISPLAY_SIZE/8 ; panel++)
  {
    for(unsigned short i = 0; i < 8 ; i++)   
    {
      display_shadow[panel][i] = 0;  
    }
  }
}

void clear_all_display_shadow(void)
{
  for(unsigned short panel = 0 ; panel < DISPLAY_SIZE/8 ; panel++)
  {
    for(unsigned short i = 0; i < 8 ; i++)   
    {
      display_shadow[panel][i] = 0;  
    }
  }
}

void update_display(void) // fixa så att själva beräkningen för nästa utklockning sker medans hårdavaran sköter nuvarande. D.v.s. lägg till en en bytes temp buffer...
{ 
    SPDR = (display_shadow[0][0] & 0xF0) | (0x0F & display_shadow[0][1]>>4);
    
    for(unsigned short int i = 1; i < DISPLAY_SIZE ; i++) 
    {   
        unsigned short int row = ((i&1)<<1);
        unsigned short int panel = i>>2;
        char tmp;
        
        if(i >= (DISPLAY_SIZE/2))
        {
          panel -= (DISPLAY_SIZE/8);
          row = row+4;
        }
    
        if(i & 0b00000010)
          tmp = (display_shadow[panel][row+1] & 0x0F) | (0xF0 & display_shadow[panel][row]<<4);
        else
          tmp = (display_shadow[panel][row] & 0xF0) | (0x0F & display_shadow[panel][row+1]>>4);  

        while(!(SPSR & (1<<SPIF))) ; //Wait for SPI process to finish
        
        SPDR = tmp;
    }

  while(!(SPSR & (1<<SPIF))) ; //Wait for SPI process to finish
  LATCH_PORT |= LATCH; //Latch into output latch
  delayMicroseconds(2);
  LATCH_PORT &= ~LATCH;
}


void clear_first_panel_display_shadow(void)
{
  for(unsigned short i = 0; i < 8 ; i++)   
  {
    display_shadow[0][i] = 0;  
  }
}

int eu_dst(const time_t * timer, int32_t * z) {
  struct tm       tmptr;
  uint8_t         month, mday, hour, day_of_week, d;
  int             n;

  /* obtain the variables */
  gmtime_r(timer, &tmptr);
  month = tmptr.tm_mon;
  day_of_week = tmptr.tm_wday;
  mday = tmptr.tm_mday - 1;
  hour = tmptr.tm_hour;

  if((month > MARCH) && (month < OCTOBER))
    return ONE_HOUR;

  if(month < MARCH)
     return 0;
  if(month > OCTOBER)
     return 0;

    /* determine mday of last Sunday */
  n = tmptr.tm_mday - 1;
  n -= day_of_week;
  n += 7;
  d = n % 7;  /* date of first Sunday */
  n = 31 - d;
  n /= 7; /* number of Sundays left in the month */
  d = d + 7 * n;  /* mday of final Sunday */
       
  if(month == MARCH) {
    if (d < mday)
       return 0;
    if (d > mday)
       return ONE_HOUR;
    if (hour < 1)
       return 0;
    return ONE_HOUR;
  }
        
  if(d < mday)
     return ONE_HOUR;
  if(d > mday)
     return 0;
  if(hour < 1)
     return ONE_HOUR;
    
  return 0;
}


ISR(WDT_vect) {

    wdt_reset();
      if(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) != CAN_OK)
    //if(CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) == CAN_OK)
    {
      Serial.println("Error Initializing MCP2515..."); 
      local_unix_time_to_display = 0;
      local_unix_time_buffer[0] = 0;
      local_unix_time_buffer[1] = 0;
      local_unix_time_buffer[2] = 0;
      
    }else{
      Serial.println("MCP2515 Initialized Successfully!");
      CAN0.setMode(MCP_NORMAL);                     // Set operation mode to normal so the MCP2515 sends acks to received data.
      local_unix_time_to_display = 0;
      local_unix_time_buffer[0] = 0;
      local_unix_time_buffer[1] = 0;
      local_unix_time_buffer[2] = 0;
      
    }
    wdt_reset();
    WDTCSR |= (1<<WDCE);
    WDTCSR |= (1<<WDIE);
    WDTCSR &= ~(1<<WDE);
}

/*
ISR(TIMER1_COMPA_vect) // interrupt runs once for every timer tick. If button holdoff is clear and button is pressed, store current time in volatile variable of corresponding player
{   
 
}
*/

