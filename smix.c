/*      SMIXW is Copyright 1995 by Ethan Brodsky.  All rights reserved      */

/* л smix.c v1.21 ллллллллллллллллллллллллллллллллллллллллллллллллллллллллл */

#define TRUE  1
#define FALSE 0

#define ON  1
#define OFF 0

#define BLOCK_LENGTH    512   /* Length of digitized sound output block     */
#define VOICES          8     /* Number of available simultaneous voices    */
#define VOLUMES         64    /* Number of volume levels for sound output   */

typedef struct
  {
    signed   char *soundptr;
    unsigned long soundsize;
  } SOUND;

int  init_sb(int baseio, int irq, int dma, int dma16);
void shutdown_sb(void);

void init_mixing(void);
void shutdown_mixing(void);

void load_sound(SOUND **sound, char *filename);
void free_sound(SOUND **sound);

void start_sound(SOUND *sound, int index, unsigned char volume, int loop);
void stop_sound(int index);

void set_sound_volume(unsigned char new_volume);

volatile long intcount;               /* Current count of sound interrupts  */
volatile int  voicecount;             /* Number of voices currently in use  */

float dspversion;
int   autoinit;
int   sixteenbit;

/* лллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллл */

// Z's script: #include <dos.h>
// Z's script: #include <i86.h>
#include <stdio.h>
#include <stdlib.h>

#include "lowmem.h"

/* extern FILE * open_sound_file(char *filename); */

#define BUFFER_LENGTH BLOCK_LENGTH*2

#define BYTE unsigned char

#define lo(value) (unsigned char)((value) & 0x00FF)
#define hi(value) (unsigned char)((value) >> 8)

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) > (b)) ? (b) : (a))

int resetport;
int readport;
int writeport;
int pollport;
int poll16port;

int pic_rotateport;
int pic_maskport;

int dma_maskport;
int dma_clrptrport;
int dma_modeport;
int dma_addrport;
int dma_countport;
int dma_pageport;

char irq_startmask;
char irq_stopmask;
char irq_intvector;

char dma_startmask;
char dma_stopmask;
char dma_mode;

void (interrupt far *oldintvector)(void);

int handler_installed;

void write_dsp(BYTE value)
  {
    while ((inp(writeport) & 0x80));
    outp(writeport, value);
  }

BYTE read_dsp(void)
  {
    while (!(inp(pollport) & 0x80));
    return(inp(readport));
  }

int reset_dsp(void)
  {
    int i;

    outp(resetport, 1);
    for (i=0; i < 100; i++)    /* Using the delay function causes a long */
      { };                     /* pause during program initialization    */
    outp(resetport, 0);
    i = 100;
    while ((read_dsp() != 0xAA) && i--);
    return(i);
  }

void install_handler(void);
void uninstall_handler(void);
void mix_exitproc(void);

int init_sb(int baseio, int irq, int dma, int dma16)
  {
   /* Sound card IO ports */
    resetport  = baseio + 0x006;
    readport   = baseio + 0x00A;
    writeport  = baseio + 0x00C;
    pollport   = baseio + 0x00E;
    poll16port = baseio + 0x00F;

   /* Reset DSP, get version, choose output mode */
    if (!reset_dsp())
      return(FALSE);
    write_dsp(0xE1);  /* Get DSP version number */
    dspversion = read_dsp();  dspversion += read_dsp() / 100.0;
    autoinit   = (dspversion > 2.0);
    sixteenbit = (dspversion > 4.0) && (dma16 != 0);

   /* Compute interrupt controller ports and parameters */
    if (irq < 8)
      { /* PIC1 */
        irq_intvector  = 0x08 + irq;
        pic_rotateport = 0x20;
        pic_maskport   = 0x21;
      }
    else
      { /* PIC2 */
        irq_intvector  = 0x70 + irq-8;
        pic_rotateport = 0xA0;
        pic_maskport   = 0xA1;
      }
    irq_stopmask  = 1 << (irq % 8);
    irq_startmask = ~irq_stopmask;

   /* Compute DMA controller ports and parameters */
    if (sixteenbit)
      { /* Sixteen bit */
        dma_maskport   = 0xD4;
        dma_clrptrport = 0xD8;
        dma_modeport   = 0xD6;
        dma_addrport   = 0xC0 + 4*(dma16-4);
        dma_countport  = 0xC2 + 4*(dma16-4);
        switch (dma16)
          {
            case 5:
              dma_pageport = 0x8B;
              break;
            case 6:
              dma_pageport = 0x89;
              break;
            case 7:
              dma_pageport = 0x8A;
              break;
          }
        dma_stopmask  = dma16-4 + 0x04;  /* 000001xx */
        dma_startmask = dma16-4 + 0x00;  /* 000000xx */
        if (autoinit)
          dma_mode = dma16-4 + 0x58;     /* 010110xx */
        else
          dma_mode = dma16-4 + 0x48;     /* 010010xx */
      }
    else
      { /* Eight bit */
        dma_maskport   = 0x0A;
        dma_clrptrport = 0x0C;
        dma_modeport   = 0x0B;
        dma_addrport   = 0x00 + 2*dma;
        dma_countport  = 0x01 + 2*dma;
        switch (dma)
          {
            case 0:
              dma_pageport = 0x87;
              break;
            case 1:
              dma_pageport = 0x83;
              break;
            case 2:
              dma_pageport = 0x81;
              break;
            case 3:
              dma_pageport = 0x82;
              break;
          }
        dma_stopmask  = dma + 0x04;      /* 000001xx */
        dma_startmask = dma + 0x00;      /* 000000xx */
        if (autoinit)
          dma_mode    = dma + 0x58;      /* 010110xx */
        else
          dma_mode    = dma + 0x48;      /* 010010xx */
      }
    install_handler();
    atexit(mix_exitproc);

    return(TRUE);
  }

void shutdown_sb(void)
  {
    if (handler_installed) uninstall_handler();
    reset_dsp();
  }

/* Voice control */

typedef struct
  {
    SOUND *sound;
    int   index;
    int   volume;
    int   loop;
    long  curpos;
    int   done;
  } VOICE;

int   inuse[VOICES];
VOICE voice[VOICES];

int curblock;

/* Volume lookup table */
signed int (*volume_table)[VOLUMES][256];

/* Mixing buffer */
signed int  mixingblock[BLOCK_LENGTH];  /* Signed 16 bit */

/* Output buffers */
void          (*outmemarea)                = NULL;  /* Twice buffer size */
unsigned char (*out8buf)[2][BLOCK_LENGTH]  = NULL;  /* Unsigned 8 bit    */
signed  short (*out16buf)[2][BLOCK_LENGTH] = NULL;  /* Signed 16 bit     */

void *blockptr[2];

short int outmemarea_sel;               /* Selector for output buffer */

/* Addressing for auto-initialized transfers (Whole buffer)   */
unsigned long buffer_addr;
unsigned char buffer_page;
unsigned int  buffer_ofs;

/* Addressing for single-cycle transfers (One block at a time */
unsigned long block_addr[2];
unsigned char block_page[2];
unsigned int  block_ofs[2];

int handler_installed;

unsigned char sound_volume;

/* 8-bit clipping */

unsigned char (*clip_8_buf)[256*VOICES];
unsigned char (*clip_8)[256*VOICES];

void start_dac(void)
  {
    if (autoinit)
      { /* Auto init DMA */
        outp(dma_maskport,   dma_stopmask);
        outp(dma_clrptrport, 0x00);
        outp(dma_modeport,   dma_mode);
        outp(dma_addrport,   lo(buffer_ofs));
        outp(dma_addrport,   hi(buffer_ofs));
        outp(dma_countport,  lo(BUFFER_LENGTH-1));
        outp(dma_countport,  hi(BUFFER_LENGTH-1));
        outp(dma_pageport,   buffer_page);
        outp(dma_maskport,   dma_startmask);
      }
    else
      { /* Single cycle DMA */
        outp(dma_maskport,   dma_stopmask);
        outp(dma_clrptrport, 0x00);
        outp(dma_modeport,   dma_mode);
        outp(dma_addrport,   lo(buffer_ofs));
        outp(dma_addrport,   hi(buffer_ofs));
        outp(dma_countport,  lo(BLOCK_LENGTH-1));
        outp(dma_countport,  hi(BLOCK_LENGTH-1));
        outp(dma_pageport,   buffer_page);
        outp(dma_maskport,   dma_startmask);
      }

    if (sixteenbit)
      { /* Sixteen bit auto-initialized: SB16 and up (DSP 4.xx)             */
        write_dsp(0x41);                /* Set sound output sampling rate   */
        write_dsp(hi(22050));
        write_dsp(lo(22050));
        write_dsp(0xB6);                /* 16-bit cmd  - D/A - A/I - FIFO   */
        write_dsp(0x10);                /* 16-bit mode - signed mono        */
        write_dsp(lo(BLOCK_LENGTH-1));
        write_dsp(hi(BLOCK_LENGTH-1));
      }
    else
      { /* Eight bit */
        if (autoinit)
          { /* Eight bit auto-initialized:  SBPro and up (DSP 2.00+)        */
            write_dsp(0xD1);            /* Turn on speaker                  */
            write_dsp(0x40);            /* Set sound output time constant   */
            write_dsp(211);             /*  = 256 - (1000000 / rate)        */
            write_dsp(0x48);            /* Set DSP block transfer size      */
            write_dsp(lo(BLOCK_LENGTH-1));
            write_dsp(hi(BLOCK_LENGTH-1));
            write_dsp(0x1C);            /* 8-bit auto-init DMA mono output  */
          }
        else
          { /* Eight bit single-cycle:  Sound Blaster (DSP 1.xx+)           */
            write_dsp(0xD1);            /* Turn on speaker                  */
            write_dsp(0x40);            /* Set sound output time constant   */
            write_dsp(211);             /*  = 256 - (1000000 / rate)        */
            write_dsp(0x14);            /* 8-bit single-cycle DMA output    */
            write_dsp(lo(BLOCK_LENGTH-1));
            write_dsp(hi(BLOCK_LENGTH-1));
          }
      }
  }

void stop_dac(void)
  {
    if (sixteenbit)
      write_dsp(0xD5);                  /* Pause 16-bit DMA sound I/O       */
    else
      {
        write_dsp(0xD0);                /* Pause 8-bit DMA sound I/O        */
        write_dsp(0xD3);                /* Turn off speaker                 */
      }

    outp(dma_maskport, dma_stopmask);   /* Stop DMA                         */
  }

/* Volume control */

void init_volume_table(void)
  {
    unsigned int  volume;
    signed   int  insample;
    signed   char invalue;

    volume_table = malloc(VOLUMES * 256 * sizeof(signed int));

    for (volume=0; volume < VOLUMES; volume++)
      for (insample = -128; insample <= 127; insample++)
        {
          invalue = insample;
          (*volume_table)[volume][(unsigned char)invalue] =
            (((float)volume/(float)(VOLUMES-1)) * 32 * invalue);
        }

    sound_volume = 255;
  }

void set_sound_volume(unsigned char new_volume)
  {
    sound_volume = new_volume;
  }

/* Mixing initialization */

void init_clip8(void)
  {
    int i;
    int value;

    clip_8_buf = malloc(256*VOICES);
    clip_8     = (char *)clip_8_buf + 128*VOICES;

    for (i = -128*VOICES; i < 128*VOICES; i++)
      {
        value = i;
        value = max(value, -128);
        value = min(value, 127);

        (*clip_8)[i] = value + 128;
      }
  }

unsigned long linear_addr(void *ptr)
  {
    return((unsigned long)(ptr));
  }

void deallocate_voice(int voicenum);

void init_mixing(void)
  {
    int i;

    for (i=0; i < VOICES; i++)
      deallocate_voice(i);
    voicecount = 0;

    if (sixteenbit)
      {
       /* Find a block of memory that does not cross a page boundary */
        out16buf = outmemarea =
          low_malloc(4*BUFFER_LENGTH, &outmemarea_sel);

        if ((((linear_addr(out16buf) >> 1) % 65536) + BUFFER_LENGTH) > 65536)
          out16buf += BUFFER_LENGTH;

        for (i=0; i<2; i++)
          blockptr[i] = &((*out16buf)[i]);

       /* DMA parameters */
        buffer_addr = linear_addr(out16buf);
        buffer_page = buffer_addr        / 65536;
        buffer_ofs  = (buffer_addr >> 1) % 65536;

        memset(out16buf, 0x00, BUFFER_LENGTH * sizeof(signed short));
      }
    else
      {
       /* Find a block of memory that does not cross a page boundary */
        out8buf = outmemarea =
          low_malloc(2*BUFFER_LENGTH, &outmemarea_sel);

        if (((linear_addr(out8buf) % 65536) + BUFFER_LENGTH) > 65536)
          out8buf += BUFFER_LENGTH;

        for (i=0; i<2; i++)
          blockptr[i] = &((*out8buf)[i]);

       /* DMA parameters */
        buffer_addr = linear_addr(out8buf);
        buffer_page = buffer_addr / 65536;
        buffer_ofs  = buffer_addr % 65536;
        for (i=0; i<2; i++)
          {
            block_addr[i] = linear_addr(blockptr[i]);
            block_page[i] = block_addr[i] / 65536;
            block_ofs[i]  = block_addr[i] % 65536;
          }
        memset(out8buf, 0x80, BUFFER_LENGTH * sizeof(unsigned char));

        init_clip8();

      }

    curblock = 0;
    intcount = 0;

    init_volume_table();
    start_dac();
  }

void shutdown_mixing(void)
  {
    stop_dac();

    free(volume_table);

    if (!sixteenbit) free(clip_8_buf);

    low_free(outmemarea_sel);
  }

/* Loading and freeing sounds */

void load_sound(SOUND **sound, char *filename)
  {
    FILE *f;
    long size;

   /* Open file and compute size */
   /*  f = open_sound_file(filename); */
    f = fopen(filename, "rb");
    fseek(f, 0, SEEK_END);  /* Move to end of file */
    size = ftell(f);        /* File size = end pos */
    fseek(f, 0, SEEK_SET);  /* Back to begining    */
   
   /* Allocate sound control structure and sound data block */
    (*sound) = (SOUND *) malloc(sizeof(SOUND));
    (*sound)->soundptr  = (signed char *)(malloc(size));
    (*sound)->soundsize = size;

   /* Read sound data and close file (Isn't flat mode nice?) */
    fread((*sound)->soundptr, sizeof(signed char), size, f);
    fclose(f);
  }

void free_sound(SOUND **sound)
  {
    free((*sound)->soundptr);
    free(*sound);
    *sound = NULL;
  }

/* Voice maintainance */

void deallocate_voice(int voicenum)
  {
    inuse[voicenum] = FALSE;
    voice[voicenum].sound  = NULL;
    voice[voicenum].index  = -1;
    voice[voicenum].volume = 0;
    voice[voicenum].curpos = -1;
    voice[voicenum].loop   = FALSE;
    voice[voicenum].done   = FALSE;
  }

void start_sound(SOUND *sound, int index, unsigned char volume, int loop)
  {
    int i, voicenum;

    voicenum = -1;
    i = 0;

    do
      {
        if (!inuse[i])
          voicenum = i;
        i++;
      }
    while ((voicenum == -1) && (i < VOICES));

    if (voicenum != -1)
      {
        voice[voicenum].sound  = sound;
        voice[voicenum].index  = index;
        voice[voicenum].volume = volume;
        voice[voicenum].curpos = 0;
        voice[voicenum].loop   = loop;
        voice[voicenum].done   = FALSE;

        inuse[voicenum] = TRUE;
        voicecount++;
      }
  }

void stop_sound(int index)
  {
    int i;

    for (i=0; i < VOICES; i++)
      if (voice[i].index == index)
        {
          voicecount--;
          deallocate_voice(i);
        }
  }

void update_voices(void)
  {
    int voicenum;

    for (voicenum=0; voicenum < VOICES; voicenum++)
      {
        if (inuse[voicenum])
          {
            if (voice[voicenum].done)
              {
                voicecount--;
                deallocate_voice(voicenum);
              }
          }
      }
  }

/* Mixing */

void mix_voice(int voicenum)
  {
    SOUND *sound;
    int   mixlength;
    signed char *sourceptr;
    signed int *volume_lookup;
    int chunklength;
    int destindex;

   /* Initialization */
    sound = voice[voicenum].sound;

    sourceptr = sound->soundptr + voice[voicenum].curpos;
    destindex = 0;

   /* Compute mix length */
    if (voice[voicenum].loop)
      mixlength = BLOCK_LENGTH;
    else
      mixlength =
       MIN(BLOCK_LENGTH, sound->soundsize - voice[voicenum].curpos);

    volume_lookup =
     (signed int *)(&((*volume_table)[(unsigned char)((((sound_volume/256.0) * voice[voicenum].volume) * (VOLUMES/256.0)))]));

    do
      {
       /* Compute the max consecutive samples that can be mixed */
        chunklength =
         MIN(mixlength, sound->soundsize - voice[voicenum].curpos);

       /* Update the current position */
        voice[voicenum].curpos += chunklength;

       /* Update the remaining samples count */
        mixlength -= chunklength;

       /* Mix samples until end of mixing or end of sound data is reached */
        while (chunklength--)
          mixingblock[destindex++] += volume_lookup[(unsigned char)(*(sourceptr++))];

       /* If we've reached the end of the block, wrap to start of sound */
        if (sourceptr == (sound->soundptr + sound->soundsize))
          {
            if (voice[voicenum].loop)
              {
                voice[voicenum].curpos = 0;
                sourceptr = sound->soundptr;
              }
            else
              {
                voice[voicenum].done = TRUE;
              }
          }
      }
    while (mixlength); /* Wrap around to finish mixing if necessary */
  }

void silenceblock(void)
  {
    memset(&mixingblock, 0x00, BLOCK_LENGTH*sizeof(signed int));
  }

void mix_voices(void)
  {
    int i;

    silenceblock();

    for (i=0; i < VOICES; i++)
      if (inuse[i])
        mix_voice(i);
  }

void copy_sound16(void)
  {
    int i;
    signed short *destptr;

    destptr   = blockptr[curblock];

    for (i=0; i < BLOCK_LENGTH; i++)
      destptr[i] = mixingblock[i];
  }

void copy_sound8(void)
  {
    int i;
    unsigned char *destptr;

    destptr   = blockptr[curblock];

    for (i=0; i < BLOCK_LENGTH; i++)
      destptr[i] = (*clip_8)[mixingblock[i] >> 5];
  }

void copy_sound(void)
  {
    if (sixteenbit)
      copy_sound16();
    else
      copy_sound8();
  }

void startblock_sc(void) /* Starts a single-cycle DMA transfer */
  {
    outp(dma_maskport,   dma_stopmask);
    outp(dma_clrptrport, 0x00);
    outp(dma_modeport,   dma_mode);
    outp(dma_addrport,   lo(block_ofs[curblock]));
    outp(dma_addrport,   hi(block_ofs[curblock]));
    outp(dma_countport,  lo(BLOCK_LENGTH-1));
    outp(dma_countport,  hi(BLOCK_LENGTH-1));
    outp(dma_pageport,   block_page[curblock]);
    outp(dma_maskport,   dma_startmask);
    write_dsp(0x14);                /* 8-bit single-cycle DMA sound output  */
    write_dsp(lo(BLOCK_LENGTH-1));
    write_dsp(hi(BLOCK_LENGTH-1));
  }

void interrupt inthandler(void)
  {
    intcount++;

    if (!autoinit)   /* Start next block quickly if not using auto-init DMA */
      {
        startblock_sc();
        copy_sound();
        curblock = !curblock;  /* Toggle block */
      }

    update_voices();
    mix_voices();

    if (autoinit)
      {
        copy_sound();
        curblock = !curblock;  /* Toggle block */
      }

    if (sixteenbit)     /* Acknowledge interrupt with sound card */
      inp(poll16port);
    else
      inp(pollport);

    outp(0xA0, 0x20);   /* Acknowledge interrupt with PIC2 */
    outp(0x20, 0x20);   /* Acknowledge interrupt with PIC1 */
  }

void install_handler(void)
  {
    _disable();  /* CLI */
    outp(pic_maskport, (inp(pic_maskport) | irq_stopmask));

    oldintvector = _dos_getvect(irq_intvector);
    _dos_setvect(irq_intvector, inthandler);

    outp(pic_maskport, (inp(pic_maskport) & irq_startmask));
    _enable();   /* STI */

    handler_installed = TRUE;
  }

void uninstall_handler(void)
  {
    _disable();  /* CLI */
    outp(pic_maskport, (inp(pic_maskport) | irq_stopmask));

    _dos_setvect(irq_intvector, oldintvector);

    _enable();   /* STI */

    handler_installed = FALSE;
  }

void mix_exitproc(void)
  {
    stop_dac();
    shutdown_sb();
  }

/* лллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллл */
