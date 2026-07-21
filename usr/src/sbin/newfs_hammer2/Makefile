.include <bsd.own.mk>

PROG=	newfs_hammer2
SRCS=	newfs_hammer2.c mkfs_hammer2.c ondisk.c subs.c xxhash.c icrc32.c
MAN=	newfs_hammer2.8

#.PATH:	${.CURDIR}/../hammer2 ${.CURDIR}/../../sys/libkern ${.CURDIR}/../../sys/hammer2/xxhash
.PATH:  ../hammer2 ../../sys/lib/libkern  ../../sys/hammer2  ../../sys/hammer2/xxhash

WARNS=	5

CFLAGS+=	-I${.CURDIR}/../../sys
CFLAGS+=	-I${.CURDIR}/../hammer2

.include <bsd.prog.mk>
