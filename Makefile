# Makefile for rixrun
#
# 16 Feb 2022
#
# Copyright (C) 2022 Matt Evans
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

ARMULATOR_SOURCES = armulator/armemu.c
ARMULATOR_SOURCES += armulator/arminit.c
ARMULATOR_SOURCES += armulator/armsupp.c
ARMULATOR_SOURCES += armulator/armvirt.c
ARMULATOR_SOURCES += armulator/armcopro.c

RR_SOURCES = main.c
RR_SOURCES += os.c
RR_SOURCES += utils.c
RR_SOURCES += zload.c

SOURCES = $(ARMULATOR_SOURCES) $(RR_SOURCES)

CFLAGS ?= -O3
INCLUDES = -Iarmulator/

all:	rixrun

rixrun:	$(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $@

clean:
	rm -f rixrun *~
