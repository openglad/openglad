//
// Parsecfg.cpp
//
// Used to read ini/cfg type text files
//

#include "parse32.h"
#include <stdio.h>
#include <string.h>

FILE* open_cfg_file(char * filename)
{
  FILE *infile;

  if ( (infile = fopen(filename, "rt")) == NULL)
   return NULL; // failed to read file

  return infile;
}

void close_cfg_file(FILE * cfgfile)
{
  fclose(cfgfile);
}

// Dump contents to screen :)
void dump_cfg_file(FILE * cfgfile)
{
  char *oneline;
  short numlines = 0;
  oneline = read_one_line(cfgfile);
  while (oneline)
  {
   printf("%d: %s\n", ++numlines, oneline);
   oneline = read_one_line(cfgfile);
  }

}

char* read_one_line(FILE *cfgfile)
{
  static char oneline[82];
  short currentpos = 0;
  short currentchar = 0;
  short result;

  while ( ( (result=(short) fscanf(cfgfile, "%c", &currentchar)) != EOF) &&
       (currentchar != '\n') &&
       (currentpos < 80) )
   oneline[currentpos++] = (char) currentchar;

  oneline[currentpos++] = 0;  // terminate string

  if ( (currentpos < 2) && (result==EOF) )  // ran out of text
   return NULL;
  else
   return oneline;
}

// Scan .cfg file, return string value of pattern in section section
char* query_cfg_value(FILE *cfgfile, char *section,
                  char *pattern)
{
  short foundsection = 0;
  char *someline;
  char sectionstring[82];
  char lefthand[80], righthand[80];
  char returnvalue[80];
  short i;

  // Set 'strings' to totally null :)
  for (i=0; i < 80; i++)
   lefthand[i] = righthand[i] = returnvalue[i] = 0;
   
  // Set to start of file ..
  fseek(cfgfile, 0, SEEK_SET);

  // Make the section pattern ..
  sectionstring[0] = 0;
  strcpy(sectionstring, "[");
  strcat(sectionstring, section);
  strcat(sectionstring, "]");

  //printf("Looking for section %s\n", sectionstring);  
  // Scan for the section ...
  while ( (!foundsection) &&
       ((someline = read_one_line(cfgfile)) != NULL) )
  {
   if (someline == NULL)
    return NULL;
   //printf("Comparing %s to %s\n", sectionstring, someline); 
   if (!strcmp(sectionstring, someline))
   {
    foundsection = 1;
    //printf("\nfound section\n");
   }
  }

  // Scan for the pattern ..
  //printf("Looking for pattern %s\n", pattern);
  foundsection = 0;
  while ( ((someline = read_one_line(cfgfile)) != NULL) )
  {
   //printf("line: %s\n", someline);
   if (someline == NULL)
    return NULL;
   //sscanf(someline, "%[A-Za-z]s%*c%s", &lefthand[0], &righthand[0]);
   //sscanf(someline, "%[A-Za-z]s", lefthand);
   sscanf(someline, "%[^=]s", lefthand);
   //printf("Lefthand got %s\n", lefthand);
   strcpy(righthand, (someline + strlen(lefthand) + 1) );
//   printf("\n%s, %s", lefthand, righthand);
   //printf("Comparing %s to %s\n", pattern, lefthand);
   if (!strcmp(pattern, lefthand))  // found our match
   {
    strcpy(returnvalue, righthand);
       //printf("QCV returns %s\n", returnvalue);
    return &returnvalue[0];
   }
   // Check for another section ..
   if (someline[0] == '[') // new section ..
    return NULL;

  }

  return NULL;
}



