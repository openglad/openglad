#include <stdio.h>
#include <stdlib.h>

#define FILENAME_SIZE 13

/*class packfileinfo
{
	public:
		long filepos;
		char name[FILENAME_SIZE];
};*/

int main(int argc, char **argv)
{
	short numfiles = (short) argc - 1;
	long *filelocation;
	long sizeoffile;
	int i;
	FILE *infile;
	FILE *outfile;
//	packfileinfo *fileinfo;

	filelocation = (long *) malloc(sizeof(long) * argc);
//	fileinfo = new packfileinfo[numfiles];

	filelocation[0] = 0;
	if (!(outfile = fopen("output.001", "w"))) return 1;

	for (i = 0; i < numfiles; i++)
	{
		if (!(infile = fopen(argv[i + 1], "rb"))) return 1;
		fseek(infile, 0, SEEK_END);
		filelocation[i] = ftell(infile);
		fseek(infile, - ftell(infile), SEEK_END);
		fwrite(infile, filelocation[i], 1, outfile);
		fclose(infile);
	}

	fclose(outfile);
}
