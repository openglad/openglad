/* gladpack.c
 * for the packing and unpacking of .001 files
 *  8/18/02, Zardus
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILENAME_SIZE 13


int pack(int argc, char **argv)
{
	short numfiles = (short) argc - 3;
	long *filelocation;
	long *filesize;
	long sizeoffile;
	int i;
	FILE *infile;
	FILE *outfile;
	char* buffer = (char*)malloc(5000000);

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

	for (i = 0; i < numfiles; i++)
	{
		fwrite(&filelocation[i], sizeof(long), 1, outfile);
		fwrite(argv[i+3], sizeof(char) * 13, 1, outfile);
	}

	sizeoffile = filelocation[numfiles];
	fwrite(&sizeoffile, sizeof(long), 1, outfile);

	for (i = 0; i < numfiles; i++)
	{
		if (!(infile = fopen(argv[i + 3], "rb"))) exit(0);
		//printf("Packing %s ",argv[i+3]);
		//printf("-- location %i ", filelocation[i]);
		//printf("-- size %d\n",filesize[i]);

		fread(buffer,filesize[i],1,infile);
		fwrite(buffer, filesize[i], 1, outfile);

		fclose(infile);
	}

	fclose(outfile);

    free(buffer);
	return 0;
}

int unpack(int argc, char **argv)
{
	long *filelocation;
	char filename[300][13];
	//int headersize;
	//int bodysize;
	short numfiles;
	long sizeoffile;
	int i;
	char* buffer = (char*)malloc(5000000);
	FILE *infile;
	FILE *outfile;

	if (!(infile = fopen(argv[2], "r")))
	{
		printf("Error: can't open input file.\n");
		return 1;
	}

	fread(buffer, 8, 1, infile);
	if (strcmp(buffer, "GladPack")) printf("Warning: this file does not appear to be a pack file.\n");

	fread(&numfiles, sizeof(short), 1, infile);
	printf("%i files in pack.\n", numfiles);

	filelocation = (long *) malloc((numfiles + 1) * sizeof(long));

	for (i = 0; i < numfiles; i++)
	{
		fread(&filelocation[i], sizeof(long), 1, infile);
		fread(filename[i], sizeof(char) * 13, 1, infile);
	}

	fread(&sizeoffile, sizeof(long), 1, infile);
	printf("Pack file is %ld bytes long.\n", sizeoffile);

	filelocation[numfiles] = sizeoffile;

	for (i = 0; i < numfiles; i++)
	{
		//printf("Extracting %s at location %i, size %i\n", filename[i], filelocation[i],
		//		filelocation[i + 1] - filelocation[i]);
		fread(buffer, filelocation[i + 1] - filelocation[i], 1, infile);

		outfile = fopen(filename[i], "w");
		fwrite(buffer, filelocation[i + 1] - filelocation[i], 1, outfile);
		fclose(outfile);
	}

	fclose(infile);
	free(buffer);
	return 0;
}



int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "p")) pack(argc, argv);
	else if (argc > 1 && !strcmp(argv[1], "u")) unpack(argc, argv);
	else
	{
		printf("Usage: gladpack (p|u) ...\n");
		return 1;
	}
	return 0;
}
