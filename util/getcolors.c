#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	char pal[768];
	FILE *file;
	int i;

	file = fopen("../our.pal","rb");
	fread(pal,1,768,file);

	for(i=0;i<256;i++) {
		printf("color %d: %d %d %d\n",i,pal[i*3],pal[i*3+1],pal[i*3+2]);
	}

//	for(i=0;i<768;i++)
//		printf("%d: %d\n",i,pal[i]);

	fclose(file);	
}
