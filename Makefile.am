SUBDIRS = util src sound
EXTRA_DIST= release-notes.txt classes.txt scen.txt pix/*.pix \
scen/*.fss start.tex glad.hlp openglad.cfg cheats.txt
#nobase_dist_data_DATA = sound start.tex
data_DATA = graphics.001 levels.001 start.tex glad.hlp openglad.cfg
#dist_sysconf_DATA= openglad.cfg

# We could do this, but a full source dist should have
# scen/ and pix/ contents anyway, so best leave this for build time
## Make sure the .001 files are really up to date
#dist-hook:
#	cd pix; ../util/gladpack p ../@PACKAGE@-@VERSION@/graphics.001 *.pix
#	cd scen; ../util/gladpack p ../@PACKAGE@-@VERSION@/levels.001 *.fss

graphics.001:
	cd pix; ../util/gladpack p ../graphics.001 *.pix

levels.001:
	cd scen; ../util/gladpack p ../levels.001 *.fss
