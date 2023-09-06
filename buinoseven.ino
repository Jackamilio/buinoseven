#include <Gamebuino-Meta.h>
#include "types.h"
#include "overworld.h"
#include "sincoslut.h"
#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 128
#define SLICE_HEIGHT 1
#define NB_BUFFERS 4
#define BUFFER_SIZE (SCREEN_WIDTH*SLICE_HEIGHT)
#define RUN_FROM_RAM __attribute__ ((section (".data")))

// Magic to get access to wait_for_transfers_done
namespace Gamebuino_Meta
{
#define DMA_DESC_COUNT (3)
  extern volatile uint32_t dma_desc_free_count;

  static inline void wait_for_transfers_done(void) {
    while (dma_desc_free_count < DMA_DESC_COUNT);
  }

  static inline void wait_for_desc_available(const uint32_t min_desc_num) {
	  while (dma_desc_free_count < min_desc_num);
  }

  static SPISettings tftSPISettings = SPISettings(24000000, MSBFIRST, SPI_MODE0);
};

// Buffers for screen data. Fill one while the previous is being sent to the screen.
uint16_t buffers[NB_BUFFERS][BUFFER_SIZE];
uint8_t bufferid = 0;

uint32_t millicurrent = 0;
uint32_t framecount = 0;

uint32_t idletime = 0;

int16_t angle = 4096;
Vec2D campos;
Vec2D camdir;

constexpr FP scrollspeed = 5;
constexpr int yorigin = 16*3;
FP altitude = 5;

uint16_t pickOverworldPixel(int x, int y) {
  if (x<0 || x>=1024 || y<0 || y>=1024) return 0;
  // int tileidx = OVERWORLD_MAP[(x>>4) + ((y&0xFFF0)<<2)];
  // int tileoffset = tileidx << 7; // 16*16/2 = 128 = <<7
  // int coloroffset = (x%16 + ((y%16)<<4))>>1;
  // uint8_t coloridx = OVERWORLD_TILES[tileoffset+coloroffset]; // two colors indices in one here
  // coloridx = x&1 ? (coloridx&0x0F) : (coloridx>>4);
  // return OVERWORLD_PALETTE[coloridx];
  uint8_t coloridx = OVERWORLD_TILES[(OVERWORLD_MAP[(x>>4) + ((y&0xFFF0)<<2)] << 7)+((x%16 + ((y%16)<<4))>>1)];
  return OVERWORLD_PALETTE[x&1 ? (coloridx&0x0F) : (coloridx>>4)];
}

void fillline(Vec2D start, Vec2D step, uint16_t* buffer) {
  for(int x=0; x<SCREEN_WIDTH; ++x) {
    buffer[x] = pickOverworldPixel((int)start(0),(int)start(1));
    start += step;
  }
}

void setup()
{
  gb.begin();
  // We aren't using the normal screen buffer, so initialize it to 0px × 0px.
  gb.display.init(0, 0, ColorMode::rgb565);
  // Max screen refresh rate for now to see what's possible
  gb.setFrameRate(50);

  SerialUSB.begin(9600);
}

void loop()
{
  while (!gb.update());

  // inputs
  if (gb.buttons.timeHeld(BUTTON_LEFT)) angle -= 0x200;
  if (gb.buttons.timeHeld(BUTTON_RIGHT)) angle += 0x200;
  if (gb.buttons.timeHeld(BUTTON_A)) campos += camdir * scrollspeed;
  if (gb.buttons.timeHeld(BUTTON_B)) campos -= camdir * scrollspeed;
  if (gb.buttons.timeHeld(BUTTON_UP)) altitude += FP(1);
  if (gb.buttons.timeHeld(BUTTON_DOWN)) altitude -= FP(1);
  camdir = {cos(angle), sin(angle)};
  Vec2D perp = {-camdir(1),camdir(0)};

  // Use the serial monitor to observe the framerate
  uint32_t newmilli = millis();
  if (newmilli - millicurrent >= 1000) {
    millicurrent = newmilli;
    SerialUSB.printf("FPS: %i, idle: %i\n", framecount,idletime);
    framecount = 0;
    idletime = 0;
  }
  else {
    ++framecount;
  }

  Gamebuino_Meta::Display_ST7735& tft = gb.tft;
  tft.setAddrWindow(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
  //initiate SPI
  SPI.beginTransaction(Gamebuino_Meta::tftSPISettings);
  tft.dataMode();

  // Loop over slices of the screen
  FP ppdalt = (SCREEN_WIDTH/2)*altitude;
  for (int slice=0; slice<SCREEN_HEIGHT/SLICE_HEIGHT;++slice) {
    // Alternate between buffers.
    uint16_t *buffer = buffers[bufferid];
    bufferid = (bufferid+1)%NB_BUFFERS;

    // BEGIN DRAWING TO BUFFER
    for(int by=0; by < SLICE_HEIGHT; ++by) {
      int y = slice*SLICE_HEIGHT + by - yorigin;
      if (y>0) {
        FP fpy = y;
        FP z = ppdalt/fpy;
        Vec2D s = campos + camdir*z;
        fillline(s-perp*z,perp*FP(z*(2.0/SCREEN_WIDTH)),&buffer[by*SCREEN_WIDTH]);
      }
      else {
        buffer = const_cast<uint16_t*>(SKYSCANLINE);
      }
    }
    // END DRAWING TO BUFFER

    uint32_t mbefore = millis();
    tft.sendBuffer(buffer, SLICE_HEIGHT * SCREEN_WIDTH);
    uint32_t mafter = millis();
    idletime += mafter-mbefore;
  }
  // Wait for the final slice to complete before leaving the function.
  uint32_t mbefore = millis();
  Gamebuino_Meta::wait_for_transfers_done();
  uint32_t mafter = millis();
  idletime += mafter-mbefore;
  tft.idleMode();
  SPI.endTransaction();
}
