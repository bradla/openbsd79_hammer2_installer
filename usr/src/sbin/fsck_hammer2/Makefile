.include <bsd.own.mk>

PROG=	fsck_hammer2
SRCS=	fsck_hammer2.c test.c ondisk.c subs.c xxhash.c icrc32.c
MAN=	fsck_hammer2.8

.PATH:  ../hammer2 ../../sys/lib/libkern  ../../sys/hammer2  ../../sys/hammer2/xxhash

WARNS=	5

CFLAGS+=	-I../../sys
CFLAGS+=	-I../hammer2

DPADD+=		${LIBCRYPTO}
LDADD+=		-lcrypto

.include <bsd.prog.mk>
