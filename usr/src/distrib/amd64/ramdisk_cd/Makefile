#	$OpenBSD: Makefile,v 1.40 2025/12/15 16:02:47 deraadt Exp $

FS=		miniroot${OSrev}.img
FSSIZE=		13312
EFICDBOOT=	eficdboot
EFICDBOOTSIZE=	350K
CDROM=		cd${OSrev}.iso
MOUNT_POINT=	/mnt
MTREE=		${UTILS}/mtree.conf
RAMDISK=	RAMDISK_CD

MOUNT_ARGS_msdos=-o-s

LISTS=		${.CURDIR}/list
UTILS=		${.CURDIR}/../../miniroot

EFIBOOT=	${DESTDIR}/usr/mdec/BOOTX64.EFI ${DESTDIR}/usr/mdec/BOOTIA32.EFI

all: ${FS} ${CDROM}

${FS}: bsd.gz
	dd if=/dev/zero of=${FS} bs=512 count=${FSSIZE}
	vnconfig -v ${FS} > vnd
	fdisk -yi -l ${FSSIZE} -b 960 -f ${DESTDIR}/usr/mdec/mbr `cat vnd`
	echo '/ *' | disklabel -wAT- `cat vnd`
	newfs -t msdos /dev/r`cat vnd`i
	mount ${MOUNT_ARGS_msdos} /dev/`cat vnd`i ${MOUNT_POINT}
	mkdir -p ${MOUNT_POINT}/efi/boot
	cp ${EFIBOOT} ${MOUNT_POINT}/efi/boot
	umount ${MOUNT_POINT}
	newfs -O 1 -m 0 -o space -i 524288 -c ${FSSIZE} /dev/r`cat vnd`a
	mount /dev/`cat vnd`a ${MOUNT_POINT}
	objcopy -S -R .comment ${DESTDIR}/usr/mdec/boot ${.OBJDIR}/boot
	installboot -v -r ${MOUNT_POINT} `cat vnd` \
	    ${DESTDIR}/usr/mdec/biosboot ${.OBJDIR}/boot
	install -c -m 555 -o root -g wheel bsd.gz ${MOUNT_POINT}/bsd
	df -i ${MOUNT_POINT}
	umount ${MOUNT_POINT}
	vnconfig -u `cat vnd`
	rm -f vnd

${EFICDBOOT}: ${EFIBOOT}
	rm -rf ${.OBJDIR}/eficdboot-dir
	mkdir -p ${.OBJDIR}/eficdboot-dir/efi/boot
	cp ${EFIBOOT} ${.OBJDIR}/eficdboot-dir/efi/boot
	makefs -t msdos -o create_size=${EFICDBOOTSIZE} ${EFICDBOOT} \
	    ${.OBJDIR}/eficdboot-dir

${CDROM}: bsd.rd ${EFICDBOOT}
	rm -rf ${.OBJDIR}/cd-dir
	mkdir -p ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	mkdir -p ${.OBJDIR}/cd-dir/etc
	echo "set image /${OSREV}/${MACHINE}/bsd.rd" > ${.OBJDIR}/cd-dir/etc/boot.conf
	cp ${.OBJDIR}/bsd.rd ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	cp ${DESTDIR}/usr/mdec/cdbr ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	cp ${DESTDIR}/usr/mdec/cdboot ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}/cdboot
	cp ${.OBJDIR}/${EFICDBOOT} ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}
	mkhybrid -a -R -T -L -l -d -D -N -o ${.OBJDIR}/${CDROM} \
	    -A "OpenBSD ${OSREV} ${MACHINE} bootonly CD" \
	    -P "Copyright (c) `date +%Y` Theo de Raadt, The OpenBSD project" \
	    -p "Theo de Raadt <deraadt@openbsd.org>" \
	    -V "OpenBSD/${MACHINE}   ${OSREV} bootonly CD" \
	    -b ${OSREV}/${MACHINE}/cdbr -c ${OSREV}/${MACHINE}/boot.catalog \
	    -e ${OSREV}/${MACHINE}/${EFICDBOOT} \
	    ${.OBJDIR}/cd-dir

# Self-contained install CD: the bootonly cd-dir plus the release sets, so the
# installer's "Location of sets? cd0" path serves them locally and no network
# (or HTTP mirror) is needed -- which is what a HAMMER2 root install requires,
# since the sets must be the ones built from this tree, not a public mirror.
# Sets are taken from SETSDIR (defaults to RELEASEDIR); any that are missing
# are skipped with a warning rather than failing the build.  bsd.rd is taken
# from ${.OBJDIR}, so it is always the ramdisk just built here.
#
# The console is chosen here rather than inherited: ${CDROM} only rebuilds
# cd-dir when the bootonly ISO is out of date, so a cd-dir left behind by a
# serial repack would otherwise silently make this CD serial too (it did).
# Default is the stock VGA boot.conf; CDSERIAL=com0 gives a serial CD.
SETSDIR?=	${RELEASEDIR}
CDSERIAL?=
CDSPEED?=	115200
CDROM_COMPLETE=	cd${OSrev}-complete.iso
COMPLETE_SETS=	base${OSrev}.tgz comp${OSrev}.tgz man${OSrev}.tgz \
		game${OSrev}.tgz xbase${OSrev}.tgz xfont${OSrev}.tgz \
		xserv${OSrev}.tgz xshare${OSrev}.tgz \
		bsd bsd.mp BUILDINFO SHA256 SHA256.sig INSTALL.${MACHINE}

# Always rebuild: the console (CDSERIAL) and the set contents are build-time
# parameters, and an up-to-date ISO would otherwise be silently reused.
complete:
	rm -f ${.OBJDIR}/${CDROM_COMPLETE}
	cd ${.CURDIR} && ${MAKE} ${CDROM_COMPLETE}

${CDROM_COMPLETE}: ${CDROM}
	@if [ -z "${SETSDIR}" ]; then \
		echo "SETSDIR (or RELEASEDIR) must be set"; exit 1; \
	fi
	@if [ ! -f ${SETSDIR}/base${OSrev}.tgz ]; then \
		echo "no base${OSrev}.tgz in ${SETSDIR} -- run 'make release' first"; \
		exit 1; \
	fi
	for f in ${COMPLETE_SETS}; do \
		if [ -f ${SETSDIR}/$$f ]; then \
			cp ${SETSDIR}/$$f \
			    ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}/$$f; \
		else \
			echo "skipping missing $$f"; \
		fi; \
	done
	cp ${.OBJDIR}/bsd.rd ${.OBJDIR}/cd-dir/${OSREV}/${MACHINE}/bsd.rd
	@if [ -n "${CDSERIAL}" ]; then \
		printf "stty ${CDSERIAL} ${CDSPEED}\nset tty ${CDSERIAL}\n" \
		    > ${.OBJDIR}/cd-dir/etc/boot.conf; \
	else \
		:> ${.OBJDIR}/cd-dir/etc/boot.conf; \
	fi
	echo "set image /${OSREV}/${MACHINE}/bsd.rd" \
	    >> ${.OBJDIR}/cd-dir/etc/boot.conf
	cat ${.OBJDIR}/cd-dir/etc/boot.conf
	mkhybrid -a -R -T -L -l -d -D -N -o ${.OBJDIR}/${CDROM_COMPLETE} \
	    -A "OpenBSD ${OSREV} ${MACHINE} install CD" \
	    -P "Copyright (c) `date +%Y` Theo de Raadt, The OpenBSD project" \
	    -p "Theo de Raadt <deraadt@openbsd.org>" \
	    -V "OpenBSD/${MACHINE}   ${OSREV} install CD" \
	    -b ${OSREV}/${MACHINE}/cdbr -c ${OSREV}/${MACHINE}/boot.catalog \
	    -e ${OSREV}/${MACHINE}/${EFICDBOOT} \
	    ${.OBJDIR}/cd-dir

MRDISKTYPE=	rdrooth2
MRMAKEFSARGS=	-o disklabel=${MRDISKTYPE},minfree=0,density=4096

bsd.gz: bsd.rd
	objcopy -g -x -R .comment -R .SUNW_ctf \
	    -K rd_root_size -K rd_root_image \
	    bsd.rd bsd.strip
	gzip -9cn bsd.strip > bsd.gz

bsd.rd: mr.fs
	cp bsd bsd.rd
	rdsetroot bsd.rd mr.fs

bsd:
	cd ${.CURDIR}/../../../sys/arch/${MACHINE}/compile/${RAMDISK} && \
	    su ${BUILDUSER} -c '${MAKE} config && ${MAKE} clean && exec ${MAKE} ${MFLAGS}'
	cp -p ${.CURDIR}/../../../sys/arch/${MACHINE}/compile/${RAMDISK}/obj/bsd bsd

mr.fs: instbin bsd
	rm -rf $@.d
	install -d -o root -g wheel $@.d
	mtree -def ${MTREE} -p $@.d -u
	CURDIR=${.CURDIR} OBJDIR=${.OBJDIR} OSrev=${OSrev} \
	    TARGDIR=$@.d UTILS=${UTILS} RELEASEDIR=${RELEASEDIR} \
	    sh ${UTILS}/runlist.sh ${LISTS}
	rm $@.d/instbin
	makefs ${MRMAKEFSARGS} $@ $@.d

instbin.mk instbin.cache instbin.c: instbin.conf
	crunchgen -E -D ${.CURDIR}/../../.. -L ${DESTDIR}/usr/lib \
	    -c instbin.c -e instbin -m instbin.mk instbin.conf

instbin: instbin.mk instbin.cache instbin.c
	${MAKE} ${MFLAGS} -f instbin.mk SRCLIBDIR=${.CURDIR}/../../../lib all

instbin.conf: ${LISTS}
	awk -f ${UTILS}/makeconf.awk  ${LISTS} > instbin.conf

unconfig:
	-umount -f ${MOUNT_POINT}
	-[ -f vnd ] && vnconfig -u `cat vnd` && rm -f vnd

.ifdef RELEASEDIR
install:
	cp bsd.gz ${RELEASEDIR}/bsd.rd
	chmod a+r ${RELEASEDIR}/bsd.rd
	cp ${FS} ${RELEASEDIR}
	cp ${CDROM} ${RELEASEDIR}
.endif

clean cleandir:
	rm -f *.core mr.fs instbin instbin.mk instbin*.cache \
	    *.o *.lo *.c bsd bsd.rd bsd.gz bsd.strip floppy*.img \
	    lib*.a lib*.olist instbin.map boot instbin.conf ${FS}
	rm -rf mr.fs.d
	rm -f ${CDROM} ${CDROM_COMPLETE} ${EFICDBOOT}
	rm -rf cd-dir eficdboot-dir

.include <bsd.obj.mk>
