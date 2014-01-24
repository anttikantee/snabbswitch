LUASRC = $(wildcard src/lua/*.lua)
LUAOBJ = $(LUASRC:.lua=.o)
CSRC   = $(wildcard src/c/*.c)
COBJ   = $(CSRC:.c=.o)

LUAJIT_O := deps/luajit/src/libluajit.a

LIBRUMP_SO	:= deps/rumprun/rumpdyn/lib/librump.so
LIBSNABBIF_SO	:= deps/rumprun/rumpdyn/lib/librumpnet_snabbif.so
RUMPREMOTE	:= deps/rumprun/rumpremote
RUMPMAKE	:= $(PWD)/deps/rumprun/rumptools/rumpmake

all: $(LUAJIT_O) $(LIBRUMP_SO) $(RUMPREMOTE) $(LIBSNABBIF_SO)
	cd src && $(MAKE)

$(LUAJIT_O): deps/luajit/Makefile
	(echo 'Building LuaJIT\n'; cd deps/luajit && $(MAKE) PREFIX=`pwd`/usr/local && $(MAKE) DESTDIR=`pwd` install)
	(cd deps/luajit/usr/local/bin; ln -fs luajit-2.1.0-alpha luajit)

$(LIBRUMP_SO): deps/rumprun/buildnb.sh
	(cd deps/rumprun && ./buildnb.sh)

$(RUMPREMOTE): $(LIBRUMP_SO)
	(cd deps/rumprun && make)

$(LIBSNABBIF_SO): $(RUMPMAKE)
	(cd deps/libsnabbif && $(RUMPMAKE) dependall && $(RUMPMAKE) install)

clean:
	(cd deps/luajit && $(MAKE) clean)
	(cd deps/libsnabbif && $(RUMPMAKE) cleandir)
	(cd deps/rumprun && $(MAKE) clean && rm -rf rump* obj)
	(cd src; $(MAKE) clean)

.SERIAL: all
