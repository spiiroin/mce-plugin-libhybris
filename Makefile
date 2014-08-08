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
	$(RM) *.so *.p *.q

mostlyclean::
	$(RM) *.o *~ *.bak

# ----------------------------------------------------------------------------
# Default flags
# ----------------------------------------------------------------------------

CPPFLAGS += -D_GNU_SOURCE
CPPFLAGS += -D_FILE_OFFSET_BITS=64

COMMON   += -Wall
COMMON   += -W
COMMON   += -Wmissing-prototypes
COMMON   += -Os
COMMON   += -g
COMMON   += -pthreads

CFLAGS   += $(COMMON)
CFLAGS   += -std=c99

CXXFLAGS += $(COMMON)

LDFLAGS  += -g
LDFLAGS  += -pthreads

LDLIBS   += -Wl,--as-needed

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

%.so : %.pic.o
	$(CC) -o $@ -shared $^ $(LDFLAGS) $(LDLIBS)

%.pic.o : %.c
	$(CC) -c -o $@ $< -fPIC $(CPPFLAGS) $(CFLAGS)

%.q : %.c
	$(CC) -E -o $@ $<

%.p : %.q
	cat $< | cproto    | sed -e 's/_Bool/bool/g' > $@
#	cat $< | cproto -S | sed -e 's/_Bool/bool/g' > $@

# ----------------------------------------------------------------------------
# Explicit dependencies
# ----------------------------------------------------------------------------

hybris.so : LDLIBS += -lhardware -lm
hybris.so : hybris.pic.o

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
