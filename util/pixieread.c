//buffers 8/8/02
//reads and displays pixie data
//useful for debugging shit
#include <stdio.h>

int main(char argc, char **argv)
{
	char filename[40];
	FILE *file;
	unsigned char numframes,x,y;
	unsigned char *data;
	int i,j,d;

	if(argc != 2) {
		printf("USAGE: pixieread file.pix\n");
		exit(0);
	}

	strcpy(filename,argv[1]);
	printf("reading pixie: %s\n",filename);

	if(!(file=fopen(filename,"rb"))) {
		printf("error while trying to open %s\n",filename);
		exit(0);
	}

	fread(&numframes,1,1,file);
	fread(&x,1,1,file);
	fread(&y,1,1,file);

	data = (unsigned char *)malloc(numframes*x*y);

	fread(data,1,(numframes*x*y),file);

	printf("=================== %s ===================\n",filename);
	printf("num of frames: %d\nx: %d\ny: %d\n",numframes,x,y);
	
	printf("data: ");

	d=0;
	for(i=0;i<y;i++) {
		for(j=0;j<x;j++) {
			printf("%3d ",data[d]);
			d++;
		}
		printf("\n      ");
	}

	printf("\n==========================================\n");

	free(data);
	fclose(file);
	return 1;
}
