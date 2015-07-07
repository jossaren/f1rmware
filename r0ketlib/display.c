#include <string.h>

#include <rad1olib/pins.h>
#include <r0ketlib/display.h>
#include <libopencm3/lpc43xx/ssp.h>
#include <libopencm3/lpc43xx/gpio.h>
#include <libopencm3/lpc43xx/scu.h>

/**************************************************************************/
/* Utility routines to manage nokia display */
/**************************************************************************/

uint8_t lcdBuffer[RESX*RESY];
uint8_t displayType;

#define TYPE_CMD    0
#define TYPE_DATA   1

void delayms(uint32_t duration){ /* XXX: do me correctly */
	uint32_t i;

	duration*=10000;

	for (i = 0; i < duration; i++)
		__asm__("nop");
}


void lcd_select() {
    /* the LCD requires 9-Bit frames */
    // Freq = PCLK / (CPSDVSR * [SCR+1])
	/* we want 120ns / bit -> 8.3 MHz. */
	/* SPI1 BASE CLOCK should be 40.8 MHz, CPSDVSR minimum is 2 */
	/* so we run at SCR=1 => =~ 10MHz */
    uint8_t serial_clock_rate = 1;
    uint8_t clock_prescale_rate = 2;

    ssp_init(LCD_SSP,
            SSP_DATA_9BITS,
            SSP_FRAME_SPI,
            SSP_CPOL_0_CPHA_0,
            serial_clock_rate,
            clock_prescale_rate,
            SSP_MODE_NORMAL,
            SSP_MASTER,
            SSP_SLAVE_OUT_ENABLE);
	
	OFF(LCD_CS);
}

void lcd_deselect() {
	ON(LCD_CS);
}

void lcdWrite(uint8_t cd, uint8_t data) {
    uint16_t frame = 0x0;

    frame = cd << 8;
    frame |= data;

	ssp_transfer(LCD_SSP, frame );
}

void lcdInit(void) {
	SETUPgout(LCD_BL_EN);
	SETUPgout(LCD_RESET);
	SETUPgout(LCD_CS);

	/* prepare SPI */
	SETUPpin(LCD_MOSI);
	SETUPpin(LCD_SCK);

	// Reset the display
    OFF(LCD_RESET);
    delayms(100); /* 1 ms */
    ON(LCD_RESET);
    delayms(100); /* 5 ms */

    lcd_select();

	static uint8_t initseq_d[] = {
		/* The controller is a PCF8833 -
		   documentation can be found online.
		 */
		0x11,              // SLEEP_OUT  (wake up)
		0x3A, 2,           // mode 8bpp  (2= 8bpp, 3= 12bpp, 5= 16bpp)
		0x36, 0b11000000,  // my,mx,v,lao,rgb,x,x,x
		0x25, 0x3a,        // set contrast
		0x29,              // display on 
		0x03,              // BSTRON (booster voltage)
		0x2A, 0, RESX-1, 
		0x2B, 0, RESY-1
	};
	uint16_t initseq_c = ~  (  /* commands: 1, data: 0 */
			(1<< 0) |
			(1<< 1) | (0<< 2) |
			(1<< 3) | (0<< 4) |
			(1<< 5) | (0<< 6) |
			(1<< 7) |
			(1<< 8) |
			(1<< 9) | (0<<10) | (0<<11) |
			(1<<12) | (0<<13) | (0<<14) |
			0);
	int i = 0;

	lcdWrite(0, 0x01); /* most color displays need the pause */
	delayms(10);

	while(i<sizeof(initseq_d)){
		lcdWrite(initseq_c&1, initseq_d[i++]);
		initseq_c = initseq_c >> 1;
	}
    lcd_deselect();
	lcdFill(0xff); /* Clear display buffer */
}

void lcdFill(char f){
    memset(lcdBuffer,f,RESX*RESY);
#if 0
    int x;
    for(x=0;x<RESX*RESY;x++) {
        lcdBuffer[x]=f;
    }
#endif
}

void lcdSetPixel(char x, char y, uint8_t f){
    if (x<0 || x> RESX || y<0 || y > RESY)
        return;
    lcdBuffer[y*RESX+x] = f;
}

uint8_t lcdGetPixel(char x, char y){
    return lcdBuffer[y*RESX+x];
}

void lcdDisplay(void) {
    lcd_select();

	uint16_t x,y;

	lcdWrite(TYPE_CMD,0x2C); // memory write (RAMWR)

	for(y=0;y<RESY;y++){
		for(x=0;x<RESX;x++){
			lcdWrite(TYPE_DATA,lcdGetPixel(x,y)); 
		};
	}
    lcd_deselect();
}

void lcdSetContrast(int c) {
    lcd_select();
	lcdWrite(TYPE_CMD,0x25);
	lcdWrite(TYPE_DATA,c);
    lcd_deselect();
}

void lcdShiftH(bool right, bool wrap) {
	uint8_t tmp;
	for (int yb = 0; yb<RESY; yb++) {
		if (right) {
			tmp = lcdBuffer[yb*RESX];
			memmove(lcdBuffer + yb*RESX,lcdBuffer + yb*RESX+1 ,RESX-1);
            lcdBuffer[yb*RESX+(RESX-1)] = wrap?tmp:0xff;
		} else {
			tmp = lcdBuffer[yb*RESX+(RESX-1)];
			memmove(lcdBuffer + yb*RESX+1,lcdBuffer + yb*RESX ,RESX-1);
			lcdBuffer[yb*RESX] = wrap?tmp:0xff;
		}
	}
}

void lcdShiftV(bool up, bool wrap) {
	uint8_t tmp[RESX];
	if (up) {
		if (wrap)
            memmove(tmp, lcdBuffer, RESX);
        else
            memset(tmp,0xff,RESX);
		memmove(lcdBuffer,lcdBuffer+RESX ,RESX*(RESY-1));
		memmove(lcdBuffer+RESX*(RESY-1),tmp,RESX);
	} else {
		if (wrap)
            memmove(tmp, lcdBuffer+RESX*(RESY-1), RESX);
        else
            memset(tmp,0xff,RESX);
		memmove(lcdBuffer+RESX,lcdBuffer ,RESX*(RESY-1));
		memmove(lcdBuffer,tmp,RESX);
	}
}

void lcdShift(int x, int y, bool wrap) {
	bool dir=true;

    if(x<0){
        dir=false;
        x=-x;
    };

    while(x-->0)
        lcdShiftH(dir, wrap);

    if(y<0){
        dir=false;
        y=-y;
    }else{
        dir=true;
    };

    while(y-->0)
        lcdShiftV(dir, wrap);
}

