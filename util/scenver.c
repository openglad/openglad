#include <stdio.h>

int main(int argv, char **argc)
{
	char temptext[10] = "XXX";
	char versionnumber = 0;
	FILE *infile;

	infile = fopen(argc[1], "r");
	fread(temptext, 3, 1, infile);
	fread(&versionnumber, 1, 1, infile);

	printf("%s is a version %d scenario\n", argc[1], versionnumber);
	return versionnumber;
}
