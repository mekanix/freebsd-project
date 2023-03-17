.if exists(${SRCTOP}/contrib/libucl/include)
.include <src.opts.mk>
.PATH:		${SRCTOP}/contrib/libucl/include
CFLAGS+=	-I${SRCTOP}/contrib/libucl/include
LIBADD=		nv ucl
.else
CFLAGS!=	pkg-config --cflags libucl
LDFLAGS!=	pkg-config --libs libucl
LDADD=		-lnv -lucl
.endif

PREFIX?=/usr/local
BINDIR=	${PREFIX}/sbin
MANDIR=	${PREFIX}/man/man
LIBDIR=	${PREFIX}/lib

PROG=	program
SRCS=	main.c

.include <bsd.prog.mk>
