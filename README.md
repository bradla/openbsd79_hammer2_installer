[Hammer2 OBSD Website](https://www.sc00ped.com/s/openbsd/ "")

# HAMMER2 on OpenBSD — Disk Setup

> ** CRITICAL: partition offset**
>
> The HAMMER2 partition `a` must start at offset **2048** — *not* the
> default offset 128 used for A6 partitions. This keeps the OpenBSD
> disklabel at sector 129 clear of HAMMER2's 64 KB volume header, which
> would otherwise overwrite it.

## 1. Create the partition

Run the interactive disklabel editor:

```sh
disklabel -E sd1
```

At the prompts:

| Prompt          | Input     | Note                          |
|-----------------|-----------|-------------------------------|
| command         | `a`       | add partition                 |
| partition       | `a`       |                               |
| offset          | `2048`    | **required** — see note above |
| size            | `*`       | rest of disk                  |
| FS type         | `HAMMER2` |                               |
| command         | `w`       | write label                   |
| command         | `q`       | quit                          |

## 2. Create the filesystem

```sh
newfs_hammer2 /dev/sd1a
```

The default PFS label is `@DATA`.

### Example output

```
Volume /dev/sd1a       size 465.76GB
checkvolu header 0 0000007470800000/0000007470c06000
---------------------------------------------
version:          2
total-size:       465.76GB (500103643136 bytes)
boot-area-size:    64.00MB (67108864 bytes)
aux-area-size:    256.00MB (268435456 bytes)
topo-reserved:      1.82GB (1954545664 bytes)
free-size:        463.62GB (497813553152 bytes)
vol-fsid:         13373b7f-d079-4119-9fc7-fb72d26e251d
sup-clid:         b6bf7827-0835-4655-92c8-fe8360de6e3c
sup-fsid:         86d3c523-f99a-4ded-acd3-b280b492d31b
PFS "LOCAL"
    clid b15456b7-6f6a-4b6f-8b85-fd1dfbf1d82c
    fsid ede4a898-ac95-47f5-a036-b099b9765c19
PFS "DATA"
    clid 4b80d00b-f5f1-4e2d-802d-42478b9fb738
    fsid 831220e0-c637-47f6-8632-46660841dc07
```
