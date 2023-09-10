#pragma once

void dmaSetCompletedCallback(uint32_t channel, void (*callback)(void));

void dmaInit(void);
void spiDma( uint32_t chnltx, void *txdata, size_t n );

void dmaStart( uint32_t channel );
void dmaWait( uint32_t );
void dmaWait2( uint32_t channel );
void memset32( uint32_t channel, void *dst, uint32_t * value, size_t n);