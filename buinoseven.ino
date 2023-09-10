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

#undef min
#include <array>

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

// tree forest!
#define NB_TREES 64
struct Tree {
  uint16_t xo;
  uint16_t yo;
  uint8_t ho;
  Vec2D pos3D;
  uint8_t height;
};

Tree forest[NB_TREES];
Array<uint8_t,NB_TREES> treeorder;

// void insertTreeAt(int pos, int treeid) {
//   treeorder.push_back(0);
//   uint8_t* array = treeorder.data();
//   memmove8(array+pos+1,array+pos,treeorder.size()-pos);
//   array[pos] = treeid;
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
uint16_t maxSliceBuffer;

FP ppdalt;


inline void dmaWaitAndTrack(uint32_t channel) {
  idletrack.start();
  dmaWait(channel);
  idletrack.end();
}

void drawSlice(int slice, uint16_t *buffer) {
  for(int by=0; by < SLICE_HEIGHT; ++by) {
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

bool worldToScreen(Vec2D wxy, FP alt, Vec2D& out, FP* secondaltout=nullptr) {
  wxy -= campos;
  wxy = {wxy(1)*camdir(0) - wxy(0)*camdir(1), wxy(0)*camdir(0) + wxy(1)*camdir(1)};
  if (wxy(1) < FP(-0.1)) return false;
  FP df(FP(SCREEN_WIDTH/2)/wxy(1));
  out(0) = wxy(0) * df + FP(SCREEN_WIDTH/2);
  out(1) = (altitude-alt) * df + FP(yorigin);
  if (secondaltout) {
    *secondaltout = (altitude-*secondaltout) * df + FP(yorigin);
  }
  return true;
}

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

  for(int i=0; i<NB_TREES;++i) {
    forest[i].xo = random(1024);
    forest[i].yo = random(1024);
    forest[i].ho = random(16,64);
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
    SerialUSB.printf("FPS: %i, idle: %i%%, slice buffer: %i%%\n", framecount,idletrack.total*wholesecond/10000,(maxSliceBuffer*100)/SLICEDRAW_BUFFER_SIZE);
    framecount = 0;
    maxSliceBuffer = 0;
    idletrack.reset();
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

  //SliceDrawing::filledsquare(10,10,255,50,0x00F0);
  treeorder.clear();

  for(int i=0; i<NB_TREES;++i) {
    Tree& curtree = forest[i];
    curtree.pos3D = {curtree.xo,curtree.yo};
    FP alt(curtree.ho);
    if (worldToScreen(curtree.pos3D,0,curtree.pos3D,&alt)) {
      uint16_t height16 = (uint16_t)(curtree.pos3D(1)-alt);
      curtree.height = height16 > 255 ? 255 : height16;

      treeorder.push_back(i);
      for(int j=treeorder.size()-2;j>=0;--j) {
        Tree& othertree = forest[treeorder[j]];
        if (curtree.pos3D(1) < othertree.pos3D(1)) {
          uint8_t tmp = treeorder[j];
          treeorder[j]=treeorder[j+1];
          treeorder[j+1]=tmp;
        }
      }
      
      //SliceDrawing::scaledsprite(0,((int16_t)twp(0))-height16/2,((int16_t)twp(1))-height16,height8,height8);
    }
  }

  for(int i=0; i<treeorder.size(); ++i) {
    Tree& curtree = forest[treeorder[i]];
    SliceDrawing::scaledsprite(0,((int16_t)curtree.pos3D(0))-curtree.height/2,((int16_t)curtree.pos3D(1))-curtree.height,curtree.height,curtree.height);
  }
  
  //SliceDrawing::scaledsprite(0,-30,-30,75,75);

  uint16_t bfv = SliceDrawing::getBufferSize();
  if (bfv > maxSliceBuffer) {
    maxSliceBuffer = bfv;
  }
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
