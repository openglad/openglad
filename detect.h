/*      SMIXW is Copyright 1995 by Ethan Brodsky.  All rights reserved      */

/* лл DETECT.H лллллллллллллллллллллллллллллллллллллллллллллллллллллллллллл */

#define TRUE 1
#define FALSE 0

extern "C" int detect_settings(int *baseio, int *irq, int *dma, int *dma16);
  /* Detects sound card settings using BLASTER environment variable */
  /* Parameters:                                                    */
  /*   baseio    Sound card base IO address                         */
  /*   irq       Sound card IRQ                                     */
  /*   dma       Sound card 8-bit DMA channel                       */
  /*   dma16     Sound card 16-bit DMA channel (0 if none)          */
  /* Returns:                                                       */
  /*   TRUE      Sound card settings detected successfully          */
  /*   FALSE     Sound card settings could not be detected          */

/* лллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллл */

