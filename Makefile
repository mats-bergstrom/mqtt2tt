######################### -*- Mode: Makefile-Gmake -*- ########################
## Copyright (C) 2018, Mats Bergstrom
## 
## File name       : Makefile
## Description     : to build mqtt2tt
## 
## Author          : Mats Bergstrom
## Created On      : Sun Dec 31 12:08:00 2023
## 
###############################################################################

CC		= gcc
CFLAGS		= -Wall -pedantic-errors -g
CPPFLAGS	= -Icfgf
LDLIBS		= -lmosquitto -lcfgf
LDFLAGS		= -Lcfgf

BINDIR		= /usr/local/bin
ETCDIR		= /usr/local/etc
LOGDIR		= /var/local/mqtt2tt
SYSTEMD_DIR	= /lib/systemd/system

BINARIES = mqtt2tt
SYSTEMD_FILES = mqtt2tt.service
ETC_FILES = mqtt2tt.cfg

CFGFGIT		= https://github.com/mats-bergstrom/cfgf.git

all: cfgf mqtt2tt

mqtt2tt: mqtt2tt.o

mqtt2tt.o: mqtt2tt.c



.PHONY: cfgf clean uninstall install

cfgf:
	if [ ! -d cfgf ] ; then git clone $(CFGFGIT) ; fi
	cd cfgf && make

really-clean:
	if [ -d cfgf ] ; then rm -rf cfgf ; fi

clean:
	rm -f *.o mqtt2tt *~ *.log .*~
	if [ -d cfgf ] ; then cd cfgf && make clean ; fi

uninstall:
	cd $(SYSTEMD_DIR); rm $(SYSTEMD_FILES)
	cd $(BINDIR); rm $(BINARIES)
	cd $(ETCDIR); rm $(ETC_FILES)

install:
	if [ ! -d $(BINDIR) ] ; then mkdir $(BINDIR); fi
	if [ ! -d $(LOGDIR) ] ; then mkdir $(LOGDIR); fi
	if [ ! -d $(ETCDIR) ] ; then mkdir $(ETCDIR); fi
	cp $(BINARIES) $(BINDIR)
	cp $(ETC_FILES) $(ETCDIR)
	cp $(SYSTEMD_FILES) $(SYSTEMD_DIR)

