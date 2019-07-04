# ----------------------------------------------------------- -*- mode: sh -*-
# Installation directories etc
# ----------------------------------------------------------------------------

NAME    ?= mce-plugin-libhybris

DESTDIR ?= /tmp/test-install-$(NAME)

_LIBDIR ?= /usr/lib

# ----------------------------------------------------------------------------
# List of targets to build
# ----------------------------------------------------------------------------

TARGETS += hybris.so

# ----------------------------------------------------------------------------
# Top level targets
# ----------------------------------------------------------------------------

.PHONY: build install clean distclean mostlyclean

build:: $(TARGETS)

install:: build

clean:: mostlyclean
	$(RM) $(TARGETS)

distclean:: clean
	$(RM) *.so *.p *.q *.i

mostlyclean::
	$(RM) *.o *~ *.bak

# ----------------------------------------------------------------------------
# Default flags
# ----------------------------------------------------------------------------

CPPFLAGS += -D_GNU_SOURCE
CPPFLAGS += -D_FILE_OFFSET_BITS=64
CPPFLAGS += -D_THREAD_SAFE
CPPFLAGS += -DMCE_HYBRIS_INTERNAL=2

COMMON   += -Wall
COMMON   += -Wextra
COMMON   += -Wmissing-prototypes
COMMON   += -Os
COMMON   += -g
COMMON   += -fvisibility=hidden

CFLAGS   += $(COMMON)
CFLAGS   += -std=c99

CXXFLAGS += $(COMMON)

LDFLAGS  += -g

LDLIBS   += -Wl,--as-needed
LDLIBS   += -lpthread

# ----------------------------------------------------------------------------
# Flags from pkg-config
# ----------------------------------------------------------------------------

PKG_NAMES += glib-2.0
PKG_NAMES += libhardware
PKG_NAMES += android-headers

maintenance  = normalize clean distclean mostlyclean
intersection = $(strip $(foreach w,$1, $(filter $w,$2)))
ifneq ($(call intersection,$(maintenance),$(MAKECMDGOALS)),)
PKG_CONFIG   ?= true
endif

ifneq ($(strip $(PKG_NAMES)),)
PKG_CONFIG   ?= pkg-config
PKG_CFLAGS   := $(shell $(PKG_CONFIG) --cflags $(PKG_NAMES))
PKG_LDLIBS   := $(shell $(PKG_CONFIG) --libs   $(PKG_NAMES))
PKG_CPPFLAGS := $(filter -D%,$(PKG_CFLAGS)) $(filter -I%,$(PKG_CFLAGS))
PKG_CFLAGS   := $(filter-out -I%, $(filter-out -D%, $(PKG_CFLAGS)))
endif

CPPFLAGS += $(PKG_CPPFLAGS)
CFLAGS   += $(PKG_CFLAGS)
LDLIBS   += $(PKG_LDLIBS)

# ----------------------------------------------------------------------------
# Implicit rules
# ----------------------------------------------------------------------------

.SUFFIXES: %.pic.o
.PRECIOUS: %.pic.o

%.so :
	$(CC) -o $@ -shared $^ $(LDFLAGS) $(LDLIBS)

%.pic.o : %.c
	$(CC) -c -o $@ $< -fPIC $(CPPFLAGS) $(CFLAGS)

%.q : %.c
	$(CC) -E -o $@ $(CPPFLAGS) $<

%.p : %.q
	cat $< | cproto    | prettyproto.py > $@
%.i : %.q
	cat $< | cproto -s | prettyproto.py > $@

preprocess: $(patsubst %.c,%.q,$(wildcard *.c))
prototypes: $(patsubst %.c,%.p,$(wildcard *.c))
locals: $(patsubst %.c,%.i,$(wildcard *.c))

# ----------------------------------------------------------------------------
# Explicit dependencies
# ----------------------------------------------------------------------------

hybris_OBJS += hybris-fb.pic.o
hybris_OBJS += hybris-lights.pic.o
hybris_OBJS += hybris-sensors.pic.o
hybris_OBJS += hybris-thread.pic.o
hybris_OBJS += plugin-api.pic.o
hybris_OBJS += plugin-config.pic.o
hybris_OBJS += plugin-logging.pic.o
hybris_OBJS += plugin-quirks.pic.o
hybris_OBJS += sysfs-led-bacon.pic.o
hybris_OBJS += sysfs-led-binary.pic.o
hybris_OBJS += sysfs-led-f5121.pic.o
hybris_OBJS += sysfs-led-hammerhead.pic.o
hybris_OBJS += sysfs-led-htcvision.pic.o
hybris_OBJS += sysfs-led-main.pic.o
hybris_OBJS += sysfs-led-redgreen.pic.o
hybris_OBJS += sysfs-led-util.pic.o
hybris_OBJS += sysfs-led-vanilla.pic.o
hybris_OBJS += sysfs-led-white.pic.o
hybris_OBJS += sysfs-val.pic.o

hybris.so : LDLIBS += -lhardware -lm
hybris.so : $(hybris_OBJS)

install:: hybris.so
	install -d -m755 $(DESTDIR)$(_LIBDIR)/mce/modules
	install -m644 hybris.so $(DESTDIR)$(_LIBDIR)/mce/modules/

# ----------------------------------------------------------------------------
# Source code normalization
# ----------------------------------------------------------------------------

.PHONY: normalize
normalize::
	normalize_whitespace -M Makefile
	normalize_whitespace -a $(wildcard *.[ch] *.cc *.cpp)
	normalize_whitespace -a $(wildcard inifiles/*.ini)

# ----------------------------------------------------------------------------
# AUTOMATIC HEADER DEPENDENCIES
# ----------------------------------------------------------------------------

.PHONY: depend
depend::
	@echo "Updating .depend"
	$(CC) -MM $(CPPFLAGS) $(MCE_CFLAGS) $(wildcard *.c) |\
	./depend_filter.py > .depend

ifneq ($(MAKECMDGOALS),depend) # not while: make depend
ifneq (,$(wildcard .depend))   # not if .depend does not exist
include .depend
endif
endif

# ----------------------------------------------------------------------------
# Hunt for excess include statements
# ----------------------------------------------------------------------------

.PHONY: headers
.SUFFIXES: %.checked

headers:: c_headers c_sources

%.checked : %
	find_unneeded_includes.py $(CPPFLAGS) $(CFLAGS) -- $<
	@touch $@

clean::
	$(RM) *.checked *.order

c_headers:: $(patsubst %,%.checked,$(wildcard *.h))
c_sources:: $(patsubst %,%.checked,$(wildcard *.c))

order::
	find_unneeded_includes.py -- $(wildcard *.h) $(wildcard *.c)
