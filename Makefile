CFLAGS=-g `sdl-config --libs --cflags`
GLADFILES=button.o effect.o game.o glad.o gladpack.o graphlib.o guy.o help.o intro.o living.o loader.o obmap.o pal32.o parse32.o picker.o pixie.o pixien.o radar.o screen.o smooth.o sound.o stats.o text.o treasure.o video.o view.o walker.o weap.o input.o
SCENFILES=scen.cpp effect.cpp game.cpp gladpack.cpp graphlib.cpp guy.cpp help.cpp intro.cpp living.cpp loader.cpp obmap.cpp pal32.cpp parse32.cpp pixie.cpp pixien.cpp radar.cpp screen.cpp smooth.cpp sound.cpp stats.cpp text.cpp treasure.cpp video.cpp view.cpp walker.cpp weap.cpp input.cpp


all: openglad openscen

openglad: $(GLADFILES)
	g++ $(CFLAGS) -o openglad $(GLADFILES)

openscen: $(SCENFILES)
	g++ $(CFLAGS) -DSCEN -o openscen $(SCENFILES)

clean:
	rm -f *.o
	rm -f openglad
	rm -f openscen

.cpp.o:
	g++ -g -c $<
