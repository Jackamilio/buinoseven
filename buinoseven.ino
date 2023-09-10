#include <FastLED.h>
#include <SPI.h>
#include <SdFat.h>
#include "config.h"
#include "gdl/btn.h"
#include "gb/Display-ST7735.h"
#include "gb/Bootloader.h"
#include "types.h"
#include "overworld.h"
#include "sincoslut.h"
#include "oc.h"
#include "fastdma.h"
#include "slicedrawing.h"

#define NB_SLICES (SCREEN_HEIGHT/SLICE_HEIGHT)
#define NB_BUFFERS 2
#define BUFFER_SIZE (SCREEN_WIDTH*SLICE_HEIGHT)
#define RUN_FROM_RAM __attribute__ ((section (".data")))

#define SPI_SPEED SD_SCK_MHZ(20)
#define sdChipSelect 26

SdFs sd;

#ifndef USE_OVERCLOCKING
#define USE_OVERCLOCKING false
#endif

#ifndef SLICE_CALLBACK
#define SLICE_CALLBACK false
#endif

//uint32_t bgColor __attribute__ ((aligned (8))) = 0x00001111;//0x0ff000ff;
uint32_t bgColor = 0x7DDF7DDF;

static SPISettings tftSPISettings = SPISettings(24000000, MSBFIRST, SPI_MODE0);

// Buffers for screen data. Fill one while the previous is being sent to the screen.
uint16_t buffers[NB_BUFFERS][BUFFER_SIZE];
uint8_t bufferid = 0;

volatile uint32_t millicurrent = 0;
volatile uint32_t framecount = 0;

int16_t angle = 4096;
Vec2D campos;
Vec2D camdir;
Vec2D camtangent;

constexpr FP scrollspeed = 5;
constexpr int yorigin = 16*3;
FP altitude = 15;

// uint16_t pickOverworldPixel(int x, int y) {
//   if (x<0 || x>=1024 || y<0 || y>=1024) return 0;
//   // int tileidx = OVERWORLD_MAP[(x>>4) + ((y&0xFFF0)<<2)];
//   // int tileoffset = tileidx << 7; // 16*16/2 = 128 = <<7
//   // int coloroffset = (x%16 + ((y%16)<<4))>>1;
//   // uint8_t coloridx = OVERWORLD_TILES[tileoffset+coloroffset]; // two colors indices in one here
//   // coloridx = x&1 ? (coloridx&0x0F) : (coloridx>>4);
//   // return OVERWORLD_PALETTE[coloridx];
//   uint8_t coloridx = OVERWORLD_TILES[(OVERWORLD_MAP[(x>>4) + ((y>>4)<<6)] << 7)+(((x%16) + ((y%16)<<4))>>1)];
//   return OVERWORLD_PALETTE[x&1 ? (coloridx&0x0F) : (coloridx>>4)];
// }

// void fillline(Vec2D start, Vec2D step, uint16_t* buffer) {
//   for(int x=0; x<SCREEN_WIDTH; ++x) {
//     buffer[x] = pickOverworldPixel((int)start(0),(int)start(1));
//     start += step;
//   }
// }

#define TFT_CS		(30u)
#define TFT_DC		(31u)

Display_ST7735 tft( TFT_CS, TFT_DC );

struct IdleTracking {
  uint32_t startStamp;
  uint32_t total;
  inline void start() {
    startStamp = millis();
  }
  inline void end() {
    total += millis() - startStamp;
  }
  inline void reset() {
    total = 0;
  }
};

IdleTracking idletrack;
FP ppdalt;


inline void dmaWaitAndTrack(uint32_t channel) {
  idletrack.start();
  dmaWait(channel);
  idletrack.end();
}

void drawSlice(int slice, uint16_t *buffer) {
  for(int by=0; by < SLICE_HEIGHT; ++by) {
//     int y = slice*SLICE_HEIGHT + by - yorigin;
//     if (y>0) {
//       FP fpy = y;
//       FP z = ppdalt/fpy;
//       Vec2D s = campos + camdir*z;
//       fillline(s-camtangent*z,camtangent*FP(z*(2.0/SCREEN_WIDTH)),&buffer[by*SCREEN_WIDTH]);
//     }
//     else {
// #if SLICE_CALLBACK
//       memcpy(&buffer[by*SCREEN_WIDTH],SKYSCANLINE,SCREEN_WIDTH*sizeof(uint16_t));
// #else
//       memset32(RAM_CHANNEL,(void*)&buffer[by*SCREEN_WIDTH],&bgColor,SCREEN_WIDTH*sizeof(uint16_t));
//       dmaWait(RAM_CHANNEL);
// #endif
//     }
    SliceDrawing::flush(&buffer[by*SCREEN_WIDTH],slice*SLICE_HEIGHT + by);
  }
}

inline void TFTStart() {
  SPI.beginTransaction(tftSPISettings);
  tft.setAddrWindow(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
  tft.dataMode();
}

inline void TFTEnd() {
  tft.idleMode();
  SPI.endTransaction();
}

#if SLICE_CALLBACK
volatile uint8_t currentSlice = 0;
void callbackDrawNextSlice() {
  if (currentSlice < NB_SLICES) {
    uint16_t *buffer = buffers[bufferid];

    if (currentSlice==0) {
      drawSlice(currentSlice, buffer);
    }

    bufferid = (bufferid+1)%NB_BUFFERS;

    spiDma(TFT_CHANNEL,buffer,BUFFER_SIZE*2);
    //callbackDrawNextSlice();
    ++currentSlice;
    if (currentSlice < NB_SLICES) {
      buffer = buffers[bufferid];
      drawSlice(currentSlice,buffer);
    }
  }
}
#endif

void setup()
{
  WDT->CTRL.bit.ENABLE = 0;

#if USE_OVERCLOCKING
  oc();
#endif

  tft.init();
  tft.setRotation( ST7735_ROTATE_DOWN );

  initButtons();

  dmaInit();
#if SLICE_CALLBACK
  dmaSetCompletedCallback(TFT_CHANNEL, callbackDrawNextSlice);
 #endif

  SPI.begin();
  sd.begin(sdChipSelect, SPI_SPEED);


  SerialUSB.begin(9600);
}

inline void showmem(uint8_t* buf, uint32_t size) {
  for(uint32_t i=0; i < size; ++i) {
    SerialUSB.printf("%.2x ",buf[i]);
  }
}

void loop()
{
  readButtons();

  // inputs
  if (keyLeft) angle -= 0x200;
  if (keyRight) angle += 0x200;
  if (keyA) campos += camdir * scrollspeed;
  if (keyB) campos -= camdir * scrollspeed;
  if (keyUp) altitude += FP(1);
  if (keyDown) altitude -= FP(1);
  if (keyHome) Bootloader::loader();

  camdir = {cos(angle), sin(angle)};
  camtangent = {-camdir(1),camdir(0)};
  //altitude = 10 + sin(framecount>>4) * FP(5);

  // Use the serial monitor to observe the framerate
  uint32_t newmilli = millis();
#if USE_OVERCLOCKING
  constexpr uint32_t wholesecond = 1000.0*(double(DESIRED_MHZ)/48.0);
#else
  constexpr uint32_t wholesecond = 1000.0;
#endif
  if (newmilli - millicurrent >= wholesecond) {
    millicurrent = newmilli;
    SerialUSB.printf("FPS: %i, idle: %i%%\n", framecount,idletrack.total*wholesecond/10000);
    framecount = 0;
    idletrack.reset();

    // uint8_t test[4];
    // *((uint32_t*)test) = 0;
    // showmem(test,4);
    // SerialUSB.print("  ...  ");
    // *((uint16_t*)(&test[1])) = 0x1234;
    // showmem(test,4);
    // SerialUSB.println(" !");
  }
  else {
    ++framecount;
  }

  ppdalt = (SCREEN_WIDTH/2)*altitude;

  SliceDrawing::clear();
  // SliceDrawing::fillcolor(0,47,0x7DDF);
  // SliceDrawing::fillcolor(48,100,0xF000);
  // SliceDrawing::fillcolor(101,127,0x000F);

  // SliceDrawing::fillcolor(0,0,0x7DDF);
  // SliceDrawing::hline(20,0,40,0xF000);

  SliceDrawing::fillcolor(0,yorigin,0x7DDF);
  for(uint8_t y=yorigin+1; y<SCREEN_HEIGHT;++y) {
    FP fpy = y-yorigin;
    FP z = ppdalt/fpy;
    SliceDrawing::fulltline(y,0,campos + camdir*z - camtangent*z,camtangent*FP(z*(2.0/SCREEN_WIDTH)));
  }

  SliceDrawing::filledsquare(10,10,255,50,0x00F0);

#if SLICE_CALLBACK
  currentSlice = 0;
  TFTStart();
  callbackDrawNextSlice();
  idletrack.start();
  while (currentSlice<NB_SLICES) {
    dmaWait(TFT_CHANNEL);
  }
  idletrack.end();
  TFTEnd();
#else
  TFTStart();
  for (int slice=0; slice<SCREEN_HEIGHT/SLICE_HEIGHT;++slice) {
    uint16_t *buffer = buffers[bufferid];
    bufferid = (bufferid+1)%NB_BUFFERS;

    drawSlice(slice,buffer);

    dmaWaitAndTrack(TFT_CHANNEL);
    spiDma(TFT_CHANNEL,buffer,BUFFER_SIZE*2);
  }
  dmaWaitAndTrack(TFT_CHANNEL);
  TFTEnd();
 #endif
}
