#include "slicedrawing.h"
#include "config.h"
#include "fastdma.h"

// temporary include just to test
#include "overworld.h"

namespace SliceDrawing {

typedef uint16_t bufferpointer;

uint8_t buffer[SLICEDRAW_BUFFER_SIZE];

bufferpointer cursors[SCREEN_HEIGHT];
bufferpointer top;

void clear() {
    for(int i=0; i<SCREEN_HEIGHT; ++i) {
        cursors[i] = i*sizeof(bufferpointer);
        ((bufferpointer*)buffer)[i] = 0;
    }
    top = SCREEN_HEIGHT*sizeof(bufferpointer);
}

template<class T>
void pushArgs(uint8_t line, const T* args) {
    if (top+sizeof(T)+sizeof(bufferpointer) > SLICEDRAW_BUFFER_SIZE) return;

    memcpy((void*)(buffer+top),(const void*)args,sizeof(T)); // copy the data in

    //*(bufferpointer*)(&buffer[cursors[line]]) = top; // tell this line where is the next data
    buffer[cursors[line]] = top;
    buffer[cursors[line]+1] = top>>8;
    top += sizeof(T);

    buffer[top]=0;
    buffer[top+1]=0;
    //*(bufferpointer*)(&buffer[top])=0;// signify the end of the stack for this line

    cursors[line] = top;// indicate the location of this end for this line
    top += sizeof(bufferpointer);//true next free data block
}

/////////////////////////////// Op ID defines ///////////////////////////////

#define OPID_FULLCLINE 1
#define OPID_FULLTLINE 2
#define OPID_SPRITESLICE 3
#define OPID_HLINE 4

/////////////////////////////// STRUCTS ///////////////////////////////

struct __packed s_fullcline {
    uint8_t struct_id; // mandatory id first
    uint16_t color;
};

struct __packed s_fulltline {
    uint8_t struct_id; // mandatory id first
    uint8_t mapid;
    Vec2D start;
    Vec2D step;
};

struct __packed s_spriteslice {
    uint8_t struct_id:8; // mandatory id first
    uint8_t spriteid:8;
    int16_t x:9;
    uint8_t spry:7;
};

struct __packed s_hline {
    uint8_t struct_id; // mandatory id first
    uint8_t x;
    uint8_t w;
    uint16_t color;
};

/////////////////////////////// GLOBAL FUNCTIONS ///////////////////////////////

void fillcolor(uint8_t y1, uint8_t y2, uint16_t color) {
    s_fullcline s{OPID_FULLCLINE,color};
    for(;y1<=y2;++y1) {
        pushArgs(y1,&s);
    }
}

void fulltline(uint8_t y, uint8_t mapid, Vec2D start, Vec2D step) {
    s_fulltline s{OPID_FULLTLINE,mapid,start,step};
    pushArgs(y,&s);
}

// todo sprite slice

void hline(uint8_t x, uint8_t y, uint8_t w, uint16_t color) {
    s_hline s{OPID_HLINE,x,w,color};
    pushArgs(y,&s);
}

void filledsquare(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color) {
    if ( x >= SCREEN_WIDTH) x = SCREEN_WIDTH-1;
    s_hline s{OPID_HLINE,x,min(w,SCREEN_WIDTH-x),color};
    uint8_t end = min(y+h,SCREEN_HEIGHT);
    for (;y<end;++y) {
        pushArgs(y,&s);
    }
}

/////////////////////////////// INTERNAL FUNCTIONS ///////////////////////////////

// fillcolor
void itnl_fillcolor(uint16_t* buffer, uint16_t color) {
    uint32_t c = color | (color<<16);
    memset32(RAM_CHANNEL,(void*)buffer,&c,SCREEN_WIDTH*sizeof(uint16_t));
    dmaWait(RAM_CHANNEL);
}
void call_fillcolor(uint16_t* buffer, void* args) {
    s_fullcline* trueargs = (s_fullcline*)args;
    itnl_fillcolor(buffer, trueargs->color);
}

// tline
uint16_t pickOverworldPixel(int x, int y) {
  if (x<0 || x>=1024 || y<0 || y>=1024) return 0;
  // int tileidx = OVERWORLD_MAP[(x>>4) + ((y&0xFFF0)<<2)];
  // int tileoffset = tileidx << 7; // 16*16/2 = 128 = <<7
  // int coloroffset = (x%16 + ((y%16)<<4))>>1;
  // uint8_t coloridx = OVERWORLD_TILES[tileoffset+coloroffset]; // two colors indices in one here
  // coloridx = x&1 ? (coloridx&0x0F) : (coloridx>>4);
  // return OVERWORLD_PALETTE[coloridx];
  uint8_t coloridx = OVERWORLD_TILES[(OVERWORLD_MAP[(x>>4) + ((y>>4)<<6)] << 7)+(((x%16) + ((y%16)<<4))>>1)];
  return OVERWORLD_PALETTE[x&1 ? (coloridx&0x0F) : (coloridx>>4)];
}
void itnl_tline(uint16_t* buffer, Vec2D start, Vec2D step) {
  for(int x=0; x<SCREEN_WIDTH; ++x) {
    buffer[x] = pickOverworldPixel((int)start(0),(int)start(1));
    start += step;
  }
}
void call_tline(uint16_t* buffer, void* args) {
    s_fulltline* trueargs = (s_fulltline*)args;
    itnl_tline(buffer, trueargs->start, trueargs->step);
}

// todo sprite slice

// hline
void itnl_hline(uint16_t* buffer, uint8_t x, uint8_t w, uint16_t color) {
    uint32_t c = color | (color<<16);
    // memset32(RAM_CHANNEL,(void*)buffer+x*2,&c,w*2);
    // dmaWait2(RAM_CHANNEL);
    memset(buffer+x,c,w*2);
}
void call_hline(uint16_t* buffer, void* args) {
    s_hline* trueargs = (s_hline*)args;
    itnl_hline(buffer, trueargs->x, trueargs->w, trueargs->color);
}


/////////////////////////////// CALLER ARRAY ///////////////////////////////
typedef void (*callerptr)(uint16_t*,void*);

struct caller {
    callerptr ptr;
    uint8_t size;
};

const caller callers[] = {
        {nullptr, 0},
        {call_fillcolor, sizeof(s_fullcline)},
        {call_tline, sizeof(s_fulltline)},
        {nullptr, 0},
        {call_hline, sizeof(s_hline)},
    };

/////////////////////////////// FLUSH ///////////////////////////////

uint8_t argbuffer[64];

void flush(uint16_t* linebuffer, uint8_t slice) {
    //itnl_fillcolor(linebuffer, 0x7DDF);
    bufferpointer nextentry = ((bufferpointer*)buffer)[slice];
    while (nextentry) {
        uint8_t callid = buffer[nextentry];
        const caller* c = &callers[callid];

        memcpy(argbuffer,(void*)buffer+nextentry,c->size);
        c->ptr(linebuffer,(void*)argbuffer);

        nextentry += c->size;
        nextentry = buffer[nextentry] | buffer[nextentry+1]<<8;
        //SerialUSB.printf("nextentry: %i\n", nextentry);
        //nextentry = 0;
    }
}

};