// Zardus 9/03/2002
// edits pixie files
// based on pixieread by Sean

#include <stdio.h>
#include <SDL.h>

char ourcolors[] = {
                          0,0,0,8,8,8,16,16,16,24,24,24,32,32,32,40,40,40,48,48,48,56,56,56,1,
                          1,1,9,9,9,17,17,17,25,25,25,33,33,33,41,41,41,49,49,49,57,57,57,0,
                          0,0,15,15,15,18,18,18,21,21,21,24,24,24,27,27,27,30,30,30,33,33,33,36,
                          36,36,39,39,39,42,42,42,45,45,45,48,48,48,51,51,51,54,54,54,57,57,57,57,
                          16,16,54,18,18,51,20,20,48,22,22,45,24,24,42,26,26,39,28,28,36,30,30,57,
                          0,0,52,0,0,47,0,0,42,0,0,37,0,0,32,0,0,27,0,0,22,0,0,16,
                          57,16,18,54,18,20,51,20,22,48,22,24,45,24,26,42,26,28,39,28,30,36,30,0,
                          57,0,0,52,0,0,47,0,0,42,0,0,37,0,0,32,0,0,27,0,0,22,0,16,
                          16,57,18,18,54,20,20,51,22,22,48,24,24,45,26,26,42,28,28,39,30,30,36,0,
                          0,57,0,0,52,0,0,47,0,0,42,0,0,37,0,0,32,0,0,27,0,0,22,57,
                          57,16,54,54,18,51,51,20,48,48,22,45,45,24,42,42,26,39,39,28,36,36,30,57,
                          57,0,52,52,0,47,47,0,42,42,0,37,37,0,32,32,0,27,27,0,22,22,0,57,
                          16,57,54,18,54,51,20,51,48,22,48,45,24,45,42,26,42,39,28,39,36,30,36,57,
                          0,57,52,0,52,47,0,47,42,0,42,37,0,37,32,0,32,27,0,27,22,0,22,16,
                          57,57,18,54,54,20,51,51,22,48,48,24,45,45,26,42,42,28,39,39,30,36,36,0,
                          57,57,0,52,52,0,47,47,0,42,42,0,37,37,0,32,32,0,27,27,0,22,22,57,
                          41,25,52,36,20,47,31,15,42,26,10,37,21,5,32,16,0,27,11,0,22,6,0,50,
                          40,30,45,35,25,40,30,20,35,25,15,30,20,10,25,15,5,20,10,0,15,5,0,57,
                          25,41,52,20,36,47,15,31,42,10,26,37,5,21,32,0,16,27,0,11,22,0,6,50,
                          30,40,45,25,35,40,20,30,35,15,25,30,10,20,25,5,15,20,0,10,15,0,5,0,
                          18,6,0,16,6,0,13,5,0,11,5,0,8,3,0,6,2,0,3,1,0,2,0,17,
                          17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
                          17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
                          17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,41,
                          25,57,36,20,52,31,15,47,26,10,42,21,5,37,16,0,32,11,0,27,6,0,22,40,
                          30,50,35,25,45,30,20,40,25,15,35,20,10,30,15,5,25,10,0,20,5,0,15,25,
                          41,57,23,39,55,21,37,53,19,35,51,17,33,49,15,31,47,13,29,45,11,27,43,9,
                          25,41,7,23,39,5,21,37,3,19,35,1,17,33,0,15,31,0,13,29,0,11,27,57,
                          15,0,57,21,0,57,27,0,57,33,0,57,39,0,57,45,0,57,51,0,57,57,0,57,
                          15,0,57,21,0,57,27,0,57,33,0,57,39,0,57,45,0,57,51,0,57,57,0,57,
                          37,31,51,33,27,47,28,24,43,24,20,56,35,23,52,32,24,48,30,22,44,27,19,28,
                          18,18,30,20,20,32,22,22,34,24,24,36,26,26,38,28,28,40,30,30,42,32,32
		};

int main(char argc, char **argv)
{
	FILE *file;
	unsigned char numframes, x, y, curcolor;
	unsigned char *data;
	int i, j, sizex, sizey;
	int frame = 1, mult = 3, done = 0, redowindow = 1, redopicture = 1;
	SDL_Surface *pixie;
	SDL_Event event;

	if(argc != 2) {
		printf("USAGE: pixedit file.pix\n");
		exit(0);
	}

	printf("reading pixie: %s\n",argv[1]);

	if(!(file=fopen(argv[1],"rb"))) {
		printf("error while trying to open %s\n",argv[1]);
		exit(0);
	}

	fread(&numframes,1,1,file);
	fread(&x,1,1,file);
	fread(&y,1,1,file);

	data = (unsigned char *)malloc(numframes*x*y);
	fread(data,1,(numframes*x*y),file);

	printf("=================== %s ===================\n",argv[1]);
	printf("num of frames: %d\nx: %d\ny: %d\n",numframes,x,y);
	
	SDL_Init(SDL_INIT_VIDEO);

	do
	{
		if (redowindow)
		{
			sizex = x + 16; sizey = y;
			if (sizey < 64) sizey = 64;

			pixie = SDL_SetVideoMode (sizex * mult, sizey * mult, 16, SDL_HWSURFACE | SDL_DOUBLEBUF);
			redopicture = 1;
			redowindow = 0;
		}

		if (redopicture)
		{
			printf("Drawing frame %i at %i magnification\n", frame, mult);

			for (i = 0; i < y; i++)
			{
				for (j = 0; j < x; j++)
				{
					SDL_Rect rect;
					int r, g, b, c, d;

					d = data[(frame - 1) * x * y + i * x + j];
					r = ourcolors[d * 3] * 4;
					g = ourcolors[d * 3 + 1] * 4;
					b = ourcolors[d * 3 + 2] * 4;

					rect.x = j * mult;
					rect.y = i * mult;
					rect.w = mult;
					rect.h = mult;

					c = SDL_MapRGB(pixie->format, r, g, b);

					SDL_FillRect(pixie,&rect,c);
				}
			}

			for (i = 0; i < 32; i++)
			{
				for (j = x; j < x + 8; j++)
				{
					SDL_Rect rect;
					int r, g, b, c, d;

					d = i * 8 + (j - x);
					r = ourcolors[d * 3] * 4;
					g = ourcolors[d * 3 + 1] * 4;
					b = ourcolors[d * 3 + 2] * 4;

					rect.x = x * mult + (j - x) * mult * 2;
					rect.y = i * mult * 2;
					rect.w = mult * 2;
					rect.h = mult * 2;

					printf("Making color at %i, %i\n", rect.x, rect.y);

					c = SDL_MapRGB(pixie->format, r, g, b);

					SDL_FillRect(pixie,&rect,c);
				}
			}

			SDL_UpdateRect(pixie,0,0,sizex * mult, sizey * mult);
			redopicture = 0;
		}

		SDL_WaitEvent(&event);
		switch (event.type)
		{
			case SDL_QUIT:
				done = 1;
				break;
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym)
				{
					case SDLK_q:
						done = 1;
						break;
					case SDLK_ESCAPE:
						done = 1;
						break;
					case SDLK_LEFT:
						if (frame > 1) frame--;
						break;
					case SDLK_RIGHT:
						if (frame < numframes) frame++;
						break;
					case SDLK_KP_PLUS:
						mult++;
						redowindow = 1;
						break;
					case SDLK_KP_MINUS:
						if (mult > 1) mult--;
						redowindow = 1;
						break;
					default:
						break;
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (event.button.x >= x * mult)
				{
					int mousex = event.button.x;
					int mousey = event.button.y;
					mousex -= x * mult;
					mousex /= (mult * 2);
					mousey /= (mult * 2);
					curcolor = mousey * 8 + mousex;
				}
				else
				{
					int spot;
					spot = (event.button.y / mult) * x + (event.button.x / mult);
					data[spot] = curcolor;
					redopicture = 1;
				}
				break;
			default:
				break;
		}
	}
	while (!done);


	SDL_Quit();

	free(data);
	fclose(file);
	return 1;
}
