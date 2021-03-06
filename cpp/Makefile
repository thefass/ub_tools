LIB      := lib
INC      = $(LIB)/include
LIB_SRCS = $(LIB)/src
include Makefile.inc


.PHONY: all .deps tests test install regular_install local_clean clean

all: .deps $(PROGS)
	$(MAKE) -C elasticsearch
	$(MAKE) -C cgi_progs
	$(MAKE) -C marc_tools
	$(MAKE) -C pipelines
	$(MAKE) -C test
	$(MAKE) -C tests
	$(MAKE) -C throwaways

%.o: %.cc Makefile
	@echo "Compiling $< $(OPTIMISATIONS_STATE)..."
	@$(CCC) $(CCCFLAGS) $< -c

$(PROGS): % : %.o lib/libubtue.a Makefile
	@echo "Linking $@..."
	@$(CCC) -rdynamic $(LD_FLAGS) $< -o $@ $(LIBS)

-include .deps
.deps: *.cc $(INC)/*.h
	$(MAKE_DEPS) -I $(INC) *.cc

lib/libubtue.a: $(wildcard lib/src/*.cc) $(wildcard lib/include/*.h)
	$(MAKE) -C lib

tests:
	$(MAKE) -C test

install: regular_install data_install
	$(MAKE) -C elasticsearch install
	$(MAKE) -C cgi_progs install
	$(MAKE) -C marc_tools install
	$(MAKE) -C pipelines install

regular_install: $(INSTALL_PROGS) Makefile
	@echo "Installing programs..."
	cp $(INSTALL_PROGS) /usr/local/bin/

data_install: Makefile data/*.conf data/*.map data/*.ttf data/IxTheo_Notation.csv data/stopwords.* \
              data/bibleRef/* data/crossref_downloader/* data/full_text_cache_monitor/* \
              data/translate_chainer/* data/zotero_cgi/* data/marc_check.rules data/language_models/*.lm \
              data/mime.types data/geographic_689_field_contents \
              data/bib_ref_candidates_in_titles.exclusion_list
	@echo "Installing data..."
	cp data/*.conf /usr/local/var/lib/tuelib/
	cp data/*.map /usr/local/var/lib/tuelib/
	cp data/*.ttf /usr/local/var/lib/tuelib/
	cp data/IxTheo_Notation.csv /usr/local/var/lib/tuelib/
	cp data/stopwords.* /usr/local/var/lib/tuelib/
	cp -r data/bibleRef /usr/local/var/lib/tuelib/
	cp -r data/crossref_downloader /usr/local/var/lib/tuelib/
	cp -r data/full_text_cache_monitor /usr/local/var/lib/tuelib/
	cp -r data/translate_chainer /usr/local/var/lib/tuelib/
	cp -r data/zotero_cgi /usr/local/var/lib/tuelib/
	cp -r data/marc_check.rules /usr/local/var/lib/tuelib/
	rm -f -r /usr/local/var/lib/tuelib/language_models
	mkdir --parents /usr/local/var/lib/tuelib/language_models
	cp data/language_models/*.lm /usr/local/var/lib/tuelib/language_models
	cp data/mime.types /usr/local/var/lib/tuelib/
	if [ -d $(INCROND_DIR) ]; then \
		cp data/pipeline_script_triggers $(INCROND_DIR); \
	fi
	cp data/geographic_689_field_contents /usr/local/var/lib/tuelib/
	cp data/bib_ref_candidates_in_titles.exclusion_list /usr/local/var/lib/tuelib/

local_clean:
	rm -f *.o *~ $(PROGS) .deps data/*~

clean: local_clean Makefile
	$(MAKE) -C elasticsearch clean
	$(MAKE) -C cgi_progs clean
	$(MAKE) -C lib clean
	$(MAKE) -C marc_tools clean
	$(MAKE) -C pipelines clean
	$(MAKE) -C test clean
	$(MAKE) -C tests clean
	$(MAKE) -C throwaways clean
