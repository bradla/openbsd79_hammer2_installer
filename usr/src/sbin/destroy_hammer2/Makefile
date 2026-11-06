.include <bsd.own.mk>

PROG=	destroy_hammer2
SRCS=	destroy.c ondisk.c subs.c xxhash.c icrc32.c
NOMAN=	yes

.PATH:  ${.CURDIR}/../hammer2 ${.CURDIR}/../../sys/lib/libkern  ${.CURDIR}/../../sys/hammer2  ${.CURDIR}/../../sys/hammer2/xxhash

WARNS=	5

CFLAGS+=	-I${.CURDIR}/../../sys
CFLAGS+=	-I${.CURDIR}/../hammer2

DPADD+=		${LIBCRYPTO}
LDADD+=		-lcrypto

.include <bsd.prog.mk>
