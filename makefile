# Copyright (c) 2019 Georg Brein. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright
#    notice ,this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.


SYSTEM=$(shell uname -s)
CFLAGS=-std=c99 -pedantic -O3 -Wall -D_POSIX_C_SOURCE=200112L
CFLAGS+=-D_XOPEN_SOURCE_EXTENDED
ifeq ($(SYSTEM),Linux)
CFLAGS+=-I /usr/include/ncursesw
LIBS=-lncursesw
else
LIBS=-lcurses
endif
OBJS=main.o readconf.o util.o screen.o cpu.o os.o chario.o
CONVERT_OBJS=tnylpo-convert.o readconf.o util.o

all: tnylpo tnylpo-convert

tnylpo: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

tnylpo-convert: $(CONVERT_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(CONVERT_OBJS) $(LIBS) -o $@

$(OBJS): tnylpo.h
$(CONVERT_OBJS): tnylpo.h

clean:
	rm -f $(OBJS) $(CONVERT_OBJS)

veryclean: clean
	rm -f tnylpo tnylpo-convert
