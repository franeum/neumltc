lib.name = neum_linear_timecode

inc = ../../libltc

included := \
	$(inc)/ltc.c \
	$(inc)/encoder.c \
	$(inc)/decoder.c \
	$(inc)/timecode.c

cflags  := -I$(inc) -I..

neum.ltc.dec~.class.sources := neum.ltc_decoder_tilde.c
neum.ltc.enc~.class.sources := neum.ltc_encoder_tilde.c
neum.ltc~.class.sources := neum.ltc_tilde.c

common.sources := $(included)
externalsdir := ../../executables/pd
DESTDIR := ../..
PDLIBDIR := /executables/pd
destpath := $(DESTDIR)$(PDLIBDIR)

include ../Makefile.pdlibbuilder

install-local: all
	$(INSTALL_DIR) -v "$(destpath)"
	$(foreach v, $(executables), \
	$(INSTALL_PROGRAM) '$v' "$(destpath)";)
	$(info ++++ info: executables of lib $(lib.name) installed \
        from $(CURDIR) to $(installpath))
	$(if $(stripflags), $(strip-executables),)
