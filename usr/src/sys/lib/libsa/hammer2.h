/*	$OpenBSD$	*/
/* Standalone read-only HAMMER2 filesystem support (Milestone B). */
int	hammer2_open(char *, struct open_file *);
int	hammer2_close(struct open_file *);
int	hammer2_read(struct open_file *, void *, size_t, size_t *);
int	hammer2_write(struct open_file *, void *, size_t, size_t *);
off_t	hammer2_seek(struct open_file *, off_t, int);
int	hammer2_stat(struct open_file *, struct stat *);
int	hammer2_readdir(struct open_file *, char *);
int	hammer2_fchmod(struct open_file *, mode_t);
