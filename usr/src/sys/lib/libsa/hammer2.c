/*	$OpenBSD$	*/
/*
 * Standalone (bootblock) read-only HAMMER2 filesystem support for libsa.
 *
 * Enough of HAMMER2 to let boot(8)/efiboot load the kernel (/bsd) directly
 * off a HAMMER2 root filesystem (Milestone B of the OpenBSD HAMMER2 port).
 *
 * Read path implemented:
 *   - volume header selection (4 copies at i*2GB, highest mirror_tid)
 *   - super-root inode -> boot PFS inode (looked up by label, default "ROOT")
 *   - path resolution: DIRENT (name -> inum) then inode-by-inum under iroot
 *     (inodes hang off the PFS root keyed by inum, bit 63 clear; directory
 *     entries are keyed by dirhash, bit 63 set)
 *   - indirect-block recursion
 *   - embedded (DIRECTDATA) and block data, COMP_NONE / AUTOZERO / LZ4
 *
 * Not implemented (not needed to load a kernel): writing, ZLIB compression,
 * multi-volume images, check-code verification.
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <lib/libsa/stand.h>

/* ------------------------------------------------------------------ */
/* on-disk constants (from sys/hammer2/hammer2_disk.h)                 */
/* ------------------------------------------------------------------ */
#define H2_VOLUME_ID_HBO	0x48414d3205172011ULL
#define H2_ZONE_BYTES64		(2ULL * 1024 * 1024 * 1024)
#define H2_NUM_VOLHDRS		4
#define H2_PBUFSIZE		65536
#define H2_INODE_BYTES		1024
#define H2_SET_COUNT		4
#define H2_BLOCKREF_BYTES	128
#define H2_EMBEDDED_BYTES	512
#define H2_OFF_MASK		0xFFFFFFFFFFFFFFC0ULL
#define H2_OFF_RADIX		0x000000000000003FULL
#define H2_DIRHASH_LOMASK	0x0000000000007FFFULL

#define H2_BREF_TYPE_EMPTY	0
#define H2_BREF_TYPE_INODE	1
#define H2_BREF_TYPE_INDIRECT	2
#define H2_BREF_TYPE_DATA	3
#define H2_BREF_TYPE_DIRENT	4

#define H2_COMP_NONE		0
#define H2_COMP_AUTOZERO	1
#define H2_COMP_LZ4		2
#define H2_COMP_ZLIB		3
#define H2_DEC_COMP(n)		((n) & 15)

#define H2_OBJTYPE_DIRECTORY	1
#define H2_OBJTYPE_REGFILE	2
#define H2_OPFLAG_DIRECTDATA	0x01

/* offsets into the 128-byte blockref */
struct h2_blockref {
	uint8_t		type;
	uint8_t		methods;	/* (check<<4) | comp */
	uint8_t		copyid;
	uint8_t		keybits;	/* key range = 1 << keybits */
	uint8_t		vradix;
	uint8_t		flags;
	uint16_t	leaf_count;
	uint64_t	key;
	uint64_t	mirror_tid;
	uint64_t	modify_tid;
	uint64_t	data_off;	/* phys addr | radix(low 6 bits) */
	uint64_t	update_tid;
	uint8_t		embed[16];	/* dirent: inum(8) namlen(2) type(1) */
	uint8_t		check[64];	/* dirent filename (<=64) lives here */
} __packed;

/* field accessors on a raw 1024-byte inode image */
#define INO_TYPE(p)	(*(uint8_t  *)((char *)(p) + 0x50))
#define INO_OPFLAGS(p)	(*(uint8_t  *)((char *)(p) + 0x51))
#define INO_SIZE(p)	(*(uint64_t *)((char *)(p) + 0x60))
#define INO_NAMELEN(p)	(*(uint16_t *)((char *)(p) + 0x80))
#define INO_COMP(p)	(*(uint8_t  *)((char *)(p) + 0x83))
#define INO_FILENAME(p)	((char *)(p) + 0x100)
#define INO_BLOCKSET(p)	((struct h2_blockref *)((char *)(p) + 0x200))
#define INO_DATA(p)	((char *)(p) + 0x200)

/* field accessors on a raw 65536-byte volume header */
#define VOL_MAGIC(p)	(*(uint64_t *)((char *)(p) + 0x00))
#define VOL_MIRROR(p)	(*(uint64_t *)((char *)(p) + 0x78))
#define VOL_SROOT(p)	((struct h2_blockref *)((char *)(p) + 0x200))

/* dirent embed */
#define DIRENT_INUM(br)		(*(uint64_t *)((br)->embed + 0))
#define DIRENT_NAMLEN(br)	(*(uint16_t *)((br)->embed + 8))

#ifndef BOOT_H2_PFS_LABEL
#define BOOT_H2_PFS_LABEL	"ROOT"
#endif

/* per-open-file state */
struct h2_file {
	char		ino[H2_INODE_BYTES];	/* this file's inode */
	char		iroot[H2_INODE_BYTES];	/* PFS root inode (inum index) */
	off_t		seek;
};

/*
 * Standalone LZ4 block-format decompressor (compatible with the kernel's
 * LZ4_decompress_safe).  HAMMER2 stores a compressed data block as
 * [int32 compressed_size][lz4 stream].  Returns decompressed byte count, or
 * -1 on malformed input.  Needs no allocation.
 */
static int
lz4_decompress_block(const char *src, char *dst, int srclen, int dstcap)
{
	const unsigned char *ip = (const unsigned char *)src;
	const unsigned char *iend = ip + srclen;
	unsigned char *op = (unsigned char *)dst;
	unsigned char *oend = op + dstcap;

	while (ip < iend) {
		unsigned tok = *ip++;
		unsigned len = tok >> 4;			/* literal length */
		unsigned off, mlen;

		if (len == 15) {
			unsigned b;
			do { if (ip >= iend) return -1; b = *ip++; len += b; }
			while (b == 255);
		}
		if (op + len > oend || ip + len > iend)
			return -1;
		memcpy(op, ip, len);
		op += len; ip += len;
		if (ip >= iend)
			break;				/* final literal run */

		if (ip + 2 > iend) return -1;
		off = ip[0] | ((unsigned)ip[1] << 8); ip += 2;
		if (off == 0 || (unsigned char *)op - off < (unsigned char *)dst)
			return -1;
		mlen = tok & 15;
		if (mlen == 15) {
			unsigned b;
			do { if (ip >= iend) return -1; b = *ip++; mlen += b; }
			while (b == 255);
		}
		mlen += 4;					/* min match */
		if (op + mlen > oend)
			return -1;
		{
			unsigned char *m = op - off;
			while (mlen--) *op++ = *m++;	/* overlap-safe byte copy */
		}
	}
	return (int)(op - (unsigned char *)dst);
}

/* ------------------------------------------------------------------ */
/* CRC32C (iSCSI) + directory hash                                     */
/* ------------------------------------------------------------------ */
static uint32_t
h2_icrc32(const void *buf, size_t len)
{
	const uint8_t *p = buf;
	uint32_t crc = 0xFFFFFFFFU;
	size_t i;
	int k;

	for (i = 0; i < len; i++) {
		crc ^= p[i];
		for (k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0x82F63B78U & (uint32_t)(-(int32_t)(crc & 1)));
	}
	return crc ^ 0xFFFFFFFFU;
}

static uint64_t
h2_dirhash(const char *name, size_t len)
{
	uint32_t crcx = 0;
	uint64_t key = 0;
	size_t i, j;

	for (i = j = 0; i < len; i++) {
		char c = name[i];
		if (c == '.' || c == '-' || c == '_' || c == '~') {
			if (i != j)
				crcx += h2_icrc32(name + j, i - j);
			j = i + 1;
		}
	}
	if (i != j)
		crcx += h2_icrc32(name + j, i - j);
	crcx |= 0x80000000U;
	key |= (uint64_t)crcx << 32;

	crcx = h2_icrc32(name, len);
	crcx = crcx ^ (crcx << 16);
	key |= crcx & 0xFFFF0000U;
	key |= 0x8000U;
	return key;
}

/* ------------------------------------------------------------------ */
/* media I/O                                                           */
/* ------------------------------------------------------------------ */
/*
 * Absolute disk byte offset of the HAMMER2 partition.  Normally 0 (the
 * bootloader's dv_strategy adds the partition's p_offset for us).  But when
 * boot spoofs a disklabel for a non-boot disk it can leave p_offset == 0 and
 * hand us absolute (whole-disk) blocks; h2_mount() then locates the partition
 * via the MBR and sets this so every read is rebased to the real partition.
 */
static uint64_t h2_base;

/* read `bytes` at HAMMER2-partition byte offset `off` (both >= 512-aligned) */
static int
h2_pread(struct open_file *f, void *buf, uint64_t off, size_t bytes)
{
	/* off is 512-aligned; use a shift so the 32-bit bootloader needs no
	 * 64-bit division helper (__udivdi3/__umoddi3 are absent in libsa). */
#if DEV_BSIZE != 512
#error "hammer2 boot reader assumes DEV_BSIZE == 512"
#endif
	char *p = buf;
	uint64_t sec = (h2_base + off) >> 9;

	/* Read in small chunks: BIOS INT13 transfers cannot cross a 64KB
	 * physical boundary and may cap the size, so never ask for too much. */
	while (bytes) {
		size_t chunk = (bytes > 8192) ? 8192 : bytes;
		size_t rsize;
		int rc;

		rc = (f->f_dev->dv_strategy)(f->f_devdata, F_READ,
		    (daddr_t)sec, chunk, p, &rsize);
		if (rc)
			return rc;
		if (rsize != chunk)
			return EIO;
		p += chunk;
		bytes -= chunk;
		sec += chunk >> 9;
	}
	return 0;
}

/* read the raw (possibly compressed) media block a blockref points at */
static int
h2_readphys(struct open_file *f, struct h2_blockref *br, void *buf,
    size_t bufsz, size_t *psize)
{
	uint64_t off = br->data_off & H2_OFF_MASK;
	int radix = (int)(br->data_off & H2_OFF_RADIX);
	size_t size;

	if (radix == 0)
		return EIO;
	size = (size_t)1 << radix;
	if (size > bufsz)
		return EIO;
	if (h2_pread(f, buf, off, size))
		return EIO;
	*psize = size;
	return 0;
}

/* ------------------------------------------------------------------ */
/* blockref tree scan                                                  */
/* ------------------------------------------------------------------ */
/*
 * Visit every leaf blockref (INODE/DATA/DIRENT) reachable from base[0..n-1]
 * whose covered key range [key, key+(1<<keybits)) intersects [kbeg,kend],
 * descending INDIRECT nodes.  cb() returning nonzero stops the scan.
 */
static int
h2_scan(struct open_file *f, struct h2_blockref *base, int n,
    uint64_t kbeg, uint64_t kend,
    int (*cb)(struct open_file *, void *, struct h2_blockref *), void *arg)
{
	int i, rc;

	for (i = 0; i < n; i++) {
		struct h2_blockref *br = &base[i];
		uint64_t lo, hi;

		if (br->type == H2_BREF_TYPE_EMPTY)
			continue;
		lo = br->key;
		hi = (br->keybits >= 64) ? ~0ULL :
		    br->key + ((1ULL << br->keybits) - 1);
		if (hi < kbeg || lo > kend)
			continue;

		if (br->type == H2_BREF_TYPE_INDIRECT) {
			void *ib = alloc(H2_PBUFSIZE);
			size_t sz;

			if (ib == NULL)
				return ENOMEM;
			rc = 0;
			if (h2_readphys(f, br, ib, H2_PBUFSIZE, &sz) == 0)
				rc = h2_scan(f, (struct h2_blockref *)ib,
				    (int)(sz / H2_BLOCKREF_BYTES),
				    kbeg, kend, cb, arg);
			free(ib, H2_PBUFSIZE);
			if (rc)
				return rc;
		} else {
			rc = cb(f, arg, br);
			if (rc)
				return rc;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* name / inum lookups                                                 */
/* ------------------------------------------------------------------ */
struct namelook {
	const char	*name;
	size_t		 nlen;
	int		 found;		/* 1 = INODE hit, 2 = DIRENT hit */
	uint64_t	 inum;		/* valid when found==2 */
	char		*inobuf;	/* 1024B: receives inode when found==1 */
};

/* compare a candidate leaf to the wanted name (mirrors chain_dirent_test) */
static int
h2_namecb(struct open_file *f, void *arg, struct h2_blockref *br)
{
	struct namelook *L = arg;

	if (br->type == H2_BREF_TYPE_INODE) {
		char *ino = L->inobuf;
		size_t sz;

		if (h2_readphys(f, br, ino, H2_INODE_BYTES, &sz))
			return 0;
		if (INO_NAMELEN(ino) == L->nlen &&
		    memcmp(INO_FILENAME(ino), L->name, L->nlen) == 0) {
			L->found = 1;
			return 1;
		}
	} else if (br->type == H2_BREF_TYPE_DIRENT) {
		if (DIRENT_NAMLEN(br) == L->nlen && L->nlen <= sizeof(br->check) &&
		    memcmp(br->check, L->name, L->nlen) == 0) {
			L->inum = DIRENT_INUM(br);
			L->found = 2;
			return 1;
		}
	}
	return 0;
}

struct inumlook {
	uint64_t	 inum;
	int		 found;
	char		*inobuf;
};

static int
h2_inumcb(struct open_file *f, void *arg, struct h2_blockref *br)
{
	struct inumlook *L = arg;
	size_t sz;

	if (br->type != H2_BREF_TYPE_INODE)
		return 0;
	if (br->key != L->inum)
		return 0;
	if (h2_readphys(f, br, L->inobuf, H2_INODE_BYTES, &sz))
		return 0;
	L->found = 1;
	return 1;
}

/* find the inode with the given inum under the PFS root (iroot) */
static int
h2_inode_by_inum(struct open_file *f, struct h2_file *fp, uint64_t inum,
    char *out)
{
	struct inumlook L;

	L.inum = inum;
	L.found = 0;
	L.inobuf = out;
	h2_scan(f, INO_BLOCKSET(fp->iroot), H2_SET_COUNT, inum, inum,
	    h2_inumcb, &L);
	return L.found ? 0 : ENOENT;
}

/*
 * Resolve one path component `name` within directory inode `dir`, placing the
 * resulting inode into `out`.  A DIRENT hit is followed by an inum lookup
 * under iroot; an INODE hit (classic / super-root) is used directly.
 */
static int
h2_lookup(struct open_file *f, struct h2_file *fp, char *dir,
    const char *name, size_t nlen, char *out)
{
	struct namelook L;
	uint64_t lhc;

	L.name = name;
	L.nlen = nlen;
	L.found = 0;
	L.inum = 0;
	L.inobuf = out;

	lhc = h2_dirhash(name, nlen);
	h2_scan(f, INO_BLOCKSET(dir), H2_SET_COUNT,
	    lhc, lhc + H2_DIRHASH_LOMASK, h2_namecb, &L);

	if (L.found == 1)
		return 0;			/* out already filled */
	if (L.found == 2)
		return h2_inode_by_inum(f, fp, L.inum, out);
	return ENOENT;
}

/* ------------------------------------------------------------------ */
/* mount: volume header -> super-root -> boot PFS (iroot)              */
/* ------------------------------------------------------------------ */
static int
h2_mount(struct open_file *f, struct h2_file *fp)
{
	char *vh, *sroot;
	struct h2_blockref *sbr;
	uint64_t best_tid = 0;
	int i, have = 0, rc;
	size_t sz;

	vh = alloc(H2_PBUFSIZE);
	sroot = alloc(H2_INODE_BYTES);
	if (vh == NULL || sroot == NULL) {
		rc = ENOMEM;
		goto out;
	}

	/*
	 * Pick the newest valid volume header among the 4 copies.  Try
	 * partition-relative first (h2_base == 0); if nothing is found the
	 * bootloader handed us absolute disk blocks (spoofed label, p_offset
	 * unapplied), so walk the MBR partition table and rebase to each
	 * partition's LBA until a volume header appears.
	 */
	{
		char *cur = alloc(H2_PBUFSIZE);
		int attempt;

		if (cur == NULL) { rc = ENOMEM; goto out; }
		h2_base = 0;

		/* DEBUG (kept permanently): probe candidate byte offsets to show
		 * where the volume header actually lands vs what dv_strategy gives. */
		{
			static const uint64_t probe[] = {
			    0, 65536ULL, 1048576ULL, 2097152ULL };
			int pi;
			for (pi = 0; pi < 4; pi++) {
				int r = h2_pread(f, cur, probe[pi], 1024);
				printf("h2dbg off=%x rc=%d magic=%x:%x\n",
				    (unsigned)probe[pi], r,
				    (unsigned)(VOL_MAGIC(cur) >> 32),
				    (unsigned)(VOL_MAGIC(cur) & 0xffffffff));
			}
		}

		for (attempt = -1; attempt < 4 && !have; attempt++) {
			if (attempt >= 0) {
				/* MBR fallback: base on partition-table entry. */
				uint8_t *e;
				uint32_t lba;

				h2_base = 0;
				if (h2_pread(f, cur, 0, 512))
					break;
				e = (uint8_t *)cur + 446 + attempt * 16;
				lba = e[8] | ((uint32_t)e[9] << 8) |
				    ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
				if (lba == 0)
					continue;
				h2_base = (uint64_t)lba << 9;
			}
			for (i = 0; i < H2_NUM_VOLHDRS; i++) {
				uint64_t off = (uint64_t)i << 31;	/* i * 2GB */
				/* only the first 1KB is needed (magic, mirror_tid,
				 * sroot_blockset all live in bytes 0x000-0x3FF) */
				if (h2_pread(f, cur, off, 1024))
					continue;
				if (VOL_MAGIC(cur) != H2_VOLUME_ID_HBO)
					continue;
				if (!have || VOL_MIRROR(cur) > best_tid) {
					memcpy(vh, cur, 1024);
					best_tid = VOL_MIRROR(cur);
					have = 1;
				}
			}
		}
		if (have)
			printf("hammer2: volume header at base=%x\n",
			    (unsigned)h2_base);
		free(cur, H2_PBUFSIZE);
	}
	if (!have) {
		printf("hammer2: no valid volume header\n");
		rc = EINVAL;
		goto out;
	}

	/* super-root inode from sroot_blockset[0] */
	sbr = VOL_SROOT(vh);
	if (sbr[0].type != H2_BREF_TYPE_INODE) {
		rc = EINVAL;
		goto out;
	}
	if (h2_readphys(f, &sbr[0], sroot, H2_INODE_BYTES, &sz)) {
		rc = EIO;
		goto out;
	}

	/* find the boot PFS (label ROOT) inode within the super-root */
	rc = h2_lookup(f, fp, sroot, BOOT_H2_PFS_LABEL,
	    strlen(BOOT_H2_PFS_LABEL), fp->iroot);
	if (rc) {
		printf("hammer2: PFS \"%s\" not found\n", BOOT_H2_PFS_LABEL);
		goto out;
	}
	rc = 0;
out:
	if (vh) free(vh, H2_PBUFSIZE);
	if (sroot) free(sroot, H2_INODE_BYTES);
	return rc;
}

/* ------------------------------------------------------------------ */
/* fs_ops                                                              */
/* ------------------------------------------------------------------ */
int
hammer2_open(char *path, struct open_file *f)
{
	struct h2_file *fp;
	char comp[256];
	char *p;
	int rc;

	fp = alloc(sizeof(*fp));
	if (fp == NULL)
		return ENOMEM;
	memset(fp, 0, sizeof(*fp));

	rc = h2_mount(f, fp);
	if (rc) {
		free(fp, sizeof(*fp));
		return rc;
	}

	/* start at the PFS root, walk the path */
	memcpy(fp->ino, fp->iroot, H2_INODE_BYTES);

	p = path;
	while (*p) {
		size_t n = 0;

		while (*p == '/')
			p++;
		if (*p == '\0')
			break;
		while (p[n] && p[n] != '/') {
			if (n >= sizeof(comp) - 1)
				break;
			comp[n] = p[n];
			n++;
		}
		comp[n] = '\0';
		p += n;

		if (INO_TYPE(fp->ino) != H2_OBJTYPE_DIRECTORY) {
			free(fp, sizeof(*fp));
			return ENOTDIR;
		}
		rc = h2_lookup(f, fp, fp->ino, comp, n, fp->ino);
		if (rc) {
			free(fp, sizeof(*fp));
			return rc;
		}
	}

	f->f_fsdata = fp;
	fp->seek = 0;
	return 0;
}

int
hammer2_close(struct open_file *f)
{
	if (f->f_fsdata)
		free(f->f_fsdata, sizeof(struct h2_file));
	f->f_fsdata = NULL;
	return 0;
}

/* find the data block covering logical offset `loff` and return it */
struct datalook {
	uint64_t	 loff;
	struct h2_blockref br;
	int		 found;
};

static int
h2_datacb(struct open_file *f, void *arg, struct h2_blockref *br)
{
	struct datalook *L = arg;

	if (br->type != H2_BREF_TYPE_DATA)
		return 0;
	L->br = *br;
	L->found = 1;
	return 1;
}

static int
h2_readblock(struct open_file *f, struct h2_file *fp, uint64_t loff,
    char *out, size_t *lsizep)
{
	struct datalook L;
	char *raw;
	size_t psize, lsize;
	int comp, rc = 0;

	L.loff = loff;
	L.found = 0;
	h2_scan(f, INO_BLOCKSET(fp->ino), H2_SET_COUNT, loff, loff,
	    h2_datacb, &L);
	if (!L.found) {
		/* sparse hole */
		memset(out, 0, H2_PBUFSIZE);
		*lsizep = H2_PBUFSIZE;
		return 0;
	}

	lsize = (L.br.keybits >= 64) ? H2_PBUFSIZE :
	    (size_t)(1ULL << L.br.keybits);
	if (lsize > H2_PBUFSIZE)
		lsize = H2_PBUFSIZE;
	comp = H2_DEC_COMP(L.br.methods);

	if (comp == H2_COMP_AUTOZERO) {
		memset(out, 0, lsize);
		*lsizep = lsize;
		return 0;
	}

	raw = alloc(H2_PBUFSIZE);
	if (raw == NULL)
		return ENOMEM;
	if (h2_readphys(f, &L.br, raw, H2_PBUFSIZE, &psize)) {
		free(raw, H2_PBUFSIZE);
		return EIO;
	}

	switch (comp) {
	case H2_COMP_NONE:
		memcpy(out, raw, lsize <= psize ? lsize : psize);
		if (lsize > psize)
			memset(out + psize, 0, lsize - psize);
		break;
	case H2_COMP_LZ4: {
		/* [int32 compressed_size][lz4 stream] -> logical block */
		int clen = *(const int *)raw;

		if (clen <= 0 || (size_t)clen > psize - sizeof(int) ||
		    lz4_decompress_block(raw + sizeof(int), out, clen,
		    (int)lsize) < 0) {
			printf("hammer2: LZ4 decompress failed\n");
			rc = EIO;
		}
		break;
	}
	default:
		printf("hammer2: unsupported compression %d "
		    "(store /bsd with 'setcomp none')\n", comp);
		rc = EINVAL;
		break;
	}
	free(raw, H2_PBUFSIZE);
	*lsizep = lsize;
	return rc;
}

int
hammer2_read(struct open_file *f, void *buf, size_t size, size_t *resid)
{
	struct h2_file *fp = f->f_fsdata;
	char *dst = buf;
	uint64_t fsize = INO_SIZE(fp->ino);
	size_t done = 0;
	int rc = 0;

	/* embedded (DIRECTDATA) small file */
	if (INO_OPFLAGS(fp->ino) & H2_OPFLAG_DIRECTDATA) {
		uint64_t avail = (fp->seek < (off_t)fsize) ?
		    fsize - fp->seek : 0;
		size_t n = (size < avail) ? size : (size_t)avail;
		if (n)
			memcpy(dst, INO_DATA(fp->ino) + fp->seek, n);
		fp->seek += n;
		done = n;
		goto done;
	}

	while (done < size && (uint64_t)fp->seek < fsize) {
		char *blk = alloc(H2_PBUFSIZE);
		uint64_t boff;
		size_t lsize, chunk, avail;

		if (blk == NULL) { rc = ENOMEM; break; }
		lsize = 0;
		/* block base for this seek: align down to the block size we get */
		rc = h2_readblock(f, fp, fp->seek, blk, &lsize);
		if (rc) { free(blk, H2_PBUFSIZE); break; }
		if (lsize == 0) lsize = H2_PBUFSIZE;

		/* lsize is a power of two -> mask instead of 64-bit modulo */
		boff = (uint64_t)fp->seek & (uint64_t)(lsize - 1);
		avail = lsize - boff;
		chunk = size - done;
		if (chunk > avail)
			chunk = avail;
		if ((uint64_t)fp->seek + chunk > fsize)
			chunk = (size_t)(fsize - fp->seek);

		memcpy(dst + done, blk + boff, chunk);
		free(blk, H2_PBUFSIZE);
		done += chunk;
		fp->seek += chunk;
		if (chunk == 0)
			break;
	}
done:
	if (resid)
		*resid = size - done;
	return rc;
}

int
hammer2_write(struct open_file *f, void *buf, size_t size, size_t *resid)
{
	return EROFS;
}

off_t
hammer2_seek(struct open_file *f, off_t offset, int where)
{
	struct h2_file *fp = f->f_fsdata;
	uint64_t fsize = INO_SIZE(fp->ino);

	switch (where) {
	case SEEK_SET:
		fp->seek = offset;
		break;
	case SEEK_CUR:
		fp->seek += offset;
		break;
	case SEEK_END:
		fp->seek = fsize - offset;
		break;
	default:
		return -1;
	}
	return fp->seek;
}

int
hammer2_stat(struct open_file *f, struct stat *sb)
{
	struct h2_file *fp = f->f_fsdata;

	memset(sb, 0, sizeof(*sb));
	sb->st_size = INO_SIZE(fp->ino);
	sb->st_mode = (INO_TYPE(fp->ino) == H2_OBJTYPE_DIRECTORY) ?
	    (S_IFDIR | 0755) : (S_IFREG | 0644);
	return 0;
}

int
hammer2_readdir(struct open_file *f, char *name)
{
	/* not needed to load a kernel */
	return EOPNOTSUPP;
}

int
hammer2_fchmod(struct open_file *f, mode_t mode)
{
	return EROFS;
}
