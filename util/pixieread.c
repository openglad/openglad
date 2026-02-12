//buffers 8/8/02
//reads and displays pixie data
//useful for debugging shit
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	FILE *file;
	unsigned char numframes,x,y;
	unsigned char *data;
	int i,j,d;

	if(argc != 2) {
		printf("USAGE: pixieread file.pix\n");
		return 1;
	}

	const char *filename = argv[1];
	printf("reading pixie: %s\n",filename);

	if(!(file=fopen(filename,"rb"))) {
		printf("error while trying to open %s\n",filename);
		return 1;
	}

	if (fread(&numframes, 1, 1, file) != 1 ||
	    fread(&x, 1, 1, file) != 1 ||
	    fread(&y, 1, 1, file) != 1)
	{
		printf("error while trying to read header from %s\n", filename);
		fclose(file);
		return 1;
	}

	size_t size = (size_t)numframes * (size_t)x * (size_t)y;
	data = (unsigned char *)malloc(size);
	if (!data) {
		printf("out of memory allocating %zu bytes\n", size);
		fclose(file);
		return 1;
	}

	if (fread(data, 1, size, file) != size) {
		printf("error while trying to read data from %s\n", filename);
		free(data);
		fclose(file);
		return 1;
	}

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
	return 0;
}
