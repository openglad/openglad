/* For making the .001 files out of normal files
 *  8/18/02, Zardus
 */
#include <stdio.h>
#include <stdlib.h>

#define FILENAME_SIZE 13

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "p")) pack(argc, argv);
	else if (argc > 1 && !strcmp(argv[1], "u")) unpack(argc, argv);
	else
	{
		printf("Usage: gladpack (p|u) ...\n");
		return 1;
	}
}

int pack(int argc, char **argv)
{
	short numfiles = (short) argc - 3;
	long *filelocation;
	long *filesize;
	long sizeoffile;
	int i;
	FILE *infile;
	FILE *outfile;
	char buffer[5000000]; //buffers: malloc is for wimps

	if (numfiles < 1)
	{
		printf("Usage: gladpack p outfile.001 file1 [file2] [file3] ...\n");
		return 1;
	}

	printf ("Creating packfile %s...\n", argv[2]);

	filelocation = (long *) malloc(sizeof(long) * (numfiles + 1));
	filesize = (long *) malloc(sizeof(long) * (numfiles + 1));

	filelocation[0] = 8 + sizeof(short) + numfiles * (13 + sizeof(long)) + sizeof(long);
	if (!(outfile = fopen(argv[2], "w"))) return 1;
	fwrite("GladPack", 8, 1, outfile);
	fwrite(&numfiles, sizeof(short), 1, outfile);

	for (i = 0; i < numfiles; i++)
	{
		if (!(infile = fopen(argv[i + 3], "rb"))) exit(0);
		fseek(infile, 0, SEEK_END);
		filesize[i] = ftell(infile);
		filelocation[i + 1] = filelocation[i] + filesize[i];

		fclose(infile);
	}

	sizeoffile = filelocation[numfiles] + filesize[numfiles - 1];
	fwrite(&sizeoffile, sizeof(long), 1, outfile);

	for (i = 0; i < numfiles; i++)
	{
		fwrite(argv[i+3], sizeof(char) * 13, 1, outfile);
		fwrite(&filelocation[i + 1], sizeof(long), 1, outfile);
	}

	for (i = 0; i < numfiles; i++)
	{
		if (!(infile = fopen(argv[i + 3], "rb"))) exit(0);
		printf("adding %s ",argv[i+3]);
		printf("-- location %i ", filelocation[i]);
		printf("-- size %d\n",filesize[i]);

		fread(buffer,filesize[i],1,infile);
		fwrite(buffer, filesize[i], 1, outfile);

		fclose(infile);
	}


	fclose(outfile);
}

int unpack(int argc, char **argv)
{
}
