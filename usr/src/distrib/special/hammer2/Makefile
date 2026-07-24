#	Crunch-only build of hammer2(8) for the install ramdisk.
#
#	WHY THIS DIRECTORY EXISTS -- do not "simplify" it away by pointing the
#	crunch at sbin/hammer2 directly:
#
#	hammer2(8) and newfs_hammer2(8) compile the SAME source files (ondisk.c,
#	subs.c, xxhash.c, icrc32.c) with the same flags, so the compiler emits
#	IDENTICAL per-translation-unit __retguard_NNN cookie symbols for both.
#	Crunched into one instbin those collide, and one program's cookie
#	reference ends up resolving to address 0: newfs_hammer2 then SIGSEGVs on
#	the very first instruction of hammer2_mkfs(),
#
#	    2fed04: mov -3140875(%rip),%r11   # 0 <__executable_start-0x200000>
#
#	which in the installer looks like "newfs_hammer2: Segmentation fault"
#	right after the disklabel is written.  (Same failure family as the
#	crc32_tab collision fixed in v10.)  Building the ramdisk's copy with
#	-fno-ret-protector removes hammer2's cookies entirely; the base
#	/sbin/hammer2 built from sbin/hammer2 keeps them.
#
#	Building here rather than in sbin/hammer2 also keeps that objdir clean:
#	the two builds would otherwise share /usr/src/sbin/hammer2/obj and the
#	base tool would silently inherit whichever flags ran last.
#
#	Deliberately NOT in distrib/special/Makefile's SUBDIR list: that target
#	links each PROG statically (LDSTATIC), which hammer2 cannot do here.
#	crunchgen finds this directory on its own via "srcdirs distrib/special",
#	which is searched before sbin.
#
#	Keep SRCS/CFLAGS in sync with sbin/hammer2/Makefile.

PROG=	hammer2
SRCS=	cmd_bulkfree.c cmd_cleanup.c hammer2_compression.c cmd_debug.c \
	cmd_destroy.c cmd_emergency.c cmd_growfs.c cmd_pfs.c cmd_recover.c \
	cmd_setcheck.c cmd_setcomp.c cmd_snapshot.c cmd_stat.c cmd_volume.c \
	hammer2_lz4.c main.c ondisk.c print_inode.c subs.c xxhash.c icrc32.c
# cluster / network cluster-controller commands (need libdmsg)
SRCS+=	cmd_service.c cmd_remote.c cmd_rsa.c cmd_info.c cmd_leaf.c

.PATH:	${.CURDIR}/../../../sbin/hammer2 \
	${.CURDIR}/../../../sys/lib/libkern \
	${.CURDIR}/../../../sys/hammer2 \
	${.CURDIR}/../../../sys/hammer2/xxhash

CFLAGS+=	-I${.CURDIR}/../../../sys
CFLAGS+=	-I${.CURDIR}/../../../lib/libdmsg	# <dmsg.h>
CFLAGS+=	-DZLIB_CONST
CFLAGS+=	-pthread

# error: 'SHA256_xxx' is deprecated [-Werror,-Wdeprecated-declarations]
CFLAGS+=	-Wno-deprecated-declarations

# the whole point of this directory -- see the comment at the top
CFLAGS+=	-fno-ret-protector

DPADD+=		${LIBCRYPTO} ${LIBZ}
LDADD+=		-ldmsg -lcrypto -lz -lpthread

.include <bsd.prog.mk>
