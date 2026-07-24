#	Crunch-only build of fsck_hammer2(8) for the install ramdisk.
#
#	Exists for the same reason as distrib/special/hammer2 -- read the long
#	comment there.  Short version: fsck_hammer2 compiles ondisk.c, subs.c,
#	xxhash.c and icrc32.c, exactly as newfs_hammer2 and hammer2 do, and the
#	compiler's per-translation-unit __retguard_NNN cookie names are not
#	unique across programs.  When two crunched programs both carry retguard
#	cookies, some of the other program's cookies end up UNDEFINED in the
#	final static link and it faults on the first cookie load (that is how
#	newfs_hammer2 died in hammer2_mkfs).  Only ONE of the hammer2 tools may
#	keep retguard inside instbin; newfs_hammer2 (built from sbin) is the one.
#
#	Keep SRCS/CFLAGS in sync with sbin/fsck_hammer2/Makefile.

PROG=	fsck_hammer2
SRCS=	fsck_hammer2.c test.c ondisk.c subs.c xxhash.c icrc32.c

.PATH:	${.CURDIR}/../../../sbin/fsck_hammer2 \
	${.CURDIR}/../../../sbin/hammer2 \
	${.CURDIR}/../../../sys/lib/libkern \
	${.CURDIR}/../../../sys/hammer2 \
	${.CURDIR}/../../../sys/hammer2/xxhash

CFLAGS+=	-I${.CURDIR}/../../../sys
CFLAGS+=	-I${.CURDIR}/../../../sbin/hammer2

# the whole point of this directory -- see above
CFLAGS+=	-fno-ret-protector

DPADD+=		${LIBCRYPTO}
LDADD+=		-lcrypto

.include <bsd.prog.mk>
