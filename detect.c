/*      SMIXW is Copyright 1995 by Ethan Brodsky.  All rights reserved      */

/* лл DETECT.C лллллллллллллллллллллллллллллллллллллллллллллллллллллллллллл */

#define TRUE  1
#define FALSE 0

int detect_settings(int *baseio, int *irq, int *dma, int *dma16);
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

#include <ctype.h>
// Z's script: #include <dos.h>
#include <stdlib.h>
#include <string.h>

#define HEX 1
#define DECIMAL 0

int get_setting(char *str, char id, int hex)
  {
    char *paramstart;
    char buf1[128];
    char buf2[128];

    strcpy(buf1, str);
    if (strchr(buf1, id) != NULL)
      {
        paramstart = strchr(buf1, id) + 1;

        if (strchr(paramstart, ' ') != NULL)
          *(strchr(paramstart, ' ')) = '\0';

        if (hex)
          strcpy(buf2, "0x");
        else
          strcpy(buf2, "");

        strcat(buf2, paramstart);

        return(strtoul(buf2, NULL, 0));
      }
    else
      return(0);
  }

int detect_settings(int *baseio, int *irq, int *dma, int *dma16)
  {
    char blaster[128];

    if (getenv("BLASTER") == NULL)
      return (FALSE);
    strupr(strcpy(blaster, getenv("BLASTER")));

    *baseio  = get_setting(blaster, 'A', HEX);
    *irq     = get_setting(blaster, 'I', DECIMAL);
    *dma     = get_setting(blaster, 'D', DECIMAL);
    *dma16   = get_setting(blaster, 'H', DECIMAL);

    if (baseio == 0) return(FALSE);
    if (irq    == 0) return(FALSE);
    if (dma    == 0) return(FALSE);
    return(TRUE);
  }
