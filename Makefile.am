SUBDIRS = util src sound

docs_DOCS = NEWS README ChangeLog release-notes.txt cheats.txt \
scen.txt AUTHORS COPYING INSTALL
docdir     = ${DESTDIR}/$(prefix)/share/doc/@PACKAGE@

EXTRA_DIST = scen/*.fss start.tex glad.hlp openglad.cfg cheats.txt pix/*.pix \
$(docs_DOCS)
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

install: install-recursive install-docs

install-docs:
	@documentations='$(docs_DOCS)'; \
	for doc in $$documentations; do \
	destdir=$(docdir); \
	name=`echo $$doc` \
	dir=$$destdir; \
	$(mkinstalldirs) $$dir; \
	$(INSTALL_DATA) $$doc $$dir/$$name; \
	  echo "installing $$doc as $$dir/$$name"; \
	done

uninstall: uninstall-recursive uninstall-docs

uninstall-docs:
	@documentations='$(docs_DOCS)'; \
	for doc in $$documentations; do \
	destdir=$(docdir); \
	name=`echo $$doc` \
	dir=$$destdir; \
	rm -f $$dir/$$name; \
	  echo "removing $$dir/$$name" ; \
	done
