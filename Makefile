all: openglad

openglad:
	make -C src/ openglad

openscen:
	make -C src/ openscen

clean:
	make -C src/ clean
