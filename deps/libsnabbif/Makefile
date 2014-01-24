LIB=	rumpnet_snabbif

SRCS=	if_virt.c
SRCS+=	component.c

RUMPTOP=${TOPRUMP}

.PATH:	${.CURDIR}/../libvirtif

CPPFLAGS+=	-I${RUMPTOP}/librump/rumpkern -I${RUMPTOP}/librump/rumpnet
CPPFLAGS+=	-I${.CURDIR}/../libvirtif

RUMPCOMP_USER=	#defined

# XXX
RUMPCOMP_USER_CPPFLAGS+=-I${.CURDIR}/../../src/apps/rumpkernel

RUMPCOMP_USER_CPPFLAGS+=-DVIRTIF_BASE=snb
CPPFLAGS+=		-DVIRTIF_BASE=snb

# XXX
RUMPCOMP_USER_CPPFLAGS+= -I${.CURDIR}/../libvirtif

.include "${RUMPTOP}/Makefile.rump"
.include <bsd.lib.mk>
.include <bsd.klinks.mk>
