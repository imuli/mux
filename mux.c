#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

enum {
	Readmax = 8,
	Muxmax = 8,
	Pathmax = 32,
};

struct muxpoint {
	char path[Pathmax];
	int rh[Readmax];
	int wh[Readmax];
	int c;
} muxs[Muxmax];
pthread_mutex_t openlock = PTHREAD_MUTEX_INITIALIZER;

static int
muxfind(const char *path){
	int i;
	pthread_mutex_lock(&openlock);
	for(i=0;i<Muxmax;i++){
		if(strcmp(muxs[i].path, path)==0) break;
	}
	if(i==Muxmax) for(i=0;i<Muxmax;i++){
		if(*muxs[i].path == 0){
			strncpy(muxs[i].path, path, Pathmax);
			break;
		}
	}
	muxs[i].c++;
	pthread_mutex_unlock(&openlock);
	if(i==Muxmax) return -ENOMEM;
	return i;
}

static int
mux_openr(const char *path){
	int i, m;
	int fds[2];

	m = muxfind(path);
	if(m < 0) return m;

	if(pipe(fds) < 0) return 0-errno;
	
	for(i=0;i<Readmax;i++){
		if(__sync_bool_compare_and_swap(&muxs[m].wh[i], 0, fds[1])) break;
	}
	if(i==Readmax){
		close(fds[0]);
		close(fds[1]);
		return -ENOMEM;
	}
	muxs[m].rh[i]=fds[0];
	return i*Muxmax+m;
}

static int
mux_open(const char *path, struct fuse_file_info *fi){
	if(strlen(path) > Pathmax-1) return -ENAMETOOLONG;
	fi->direct_io=1;
	fi->nonseekable=1;
	if ((fi->flags & 3) == O_RDONLY){
		fi->fh = mux_openr(path);
		return 0;
	}
	if ((fi->flags & 3) == O_WRONLY){
		fi->fh = muxfind(path);
		/* race? */
		return 0;
	}
	return -EACCES;
}

static int
mux_release(const char *path, struct fuse_file_info *fi){
	int i, m;
	m = fi->fh % Muxmax;
	pthread_mutex_lock(&openlock);
	if ((fi->flags & 3) == O_RDONLY){
		i = fi->fh / Muxmax;
		close(muxs[m].rh[i]);
		close(muxs[m].wh[i]);
		muxs[m].wh[i] = 0;
		muxs[m].rh[i] = 0;
	}
	muxs[m].c--;
	if(muxs[m].c==0) muxs[m].path[0]=0;
	pthread_mutex_unlock(&openlock);
	return 0;
}

static int mux_getattr(const char *path, struct stat *stbuf)
{
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0770;
		stbuf->st_nlink = 2;
	} else {
		stbuf->st_mode = S_IFREG | 0660;
		stbuf->st_nlink = 1;
		stbuf->st_size = 0;
	}
	return 0;
}

static int
mux_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
	int i;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	for(i=0;i<Muxmax;i++)
		if(*muxs[i].path == '/')
			filler(buf, muxs[i].path+1, NULL, 0);

	return 0;
}

static int
mux_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
	int m,i,r;
	m = fi->fh % Muxmax;
	i = fi->fh / Muxmax;
	r = read(muxs[m].rh[i], buf, size);
	if(r<0) return 0-errno;
	return r;
}

static int
writeall(int fd, const char *buf, size_t size){
	int r, o;
	r = o = 0;
	while(o < size){
		r = write(fd, buf+o, size-o);
		if(r<0) return 0-errno;
		o+=r;
	}
	return o;
}

static int
mux_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
	int i, r;
	for(i=0;i<Readmax;i++){
		if(muxs[fi->fh].wh[i] != 0){
			r = writeall(muxs[fi->fh].wh[i], buf, size);
			if(r<0) return r;
		}
	}
	return size;
}

int
mux_truncate(const char *path, off_t off){
	return 0;
}

static struct fuse_operations mux_oper = {
	.getattr	= mux_getattr,
	.readdir	= mux_readdir,
	.open		= mux_open,
	.read		= mux_read,
	.write		= mux_write,
	.release	= mux_release,
	.truncate	= mux_truncate,
};

void
mux_interrupt(int sig){
}

int
main(int argc, char **argv){
	struct sigaction sa;

	sa.sa_handler = mux_interrupt;
	sa.sa_flags = 0;
	sigfillset(&sa.sa_mask);
	sigaction(SIGUSR1, &sa, NULL);
	
	return fuse_main(argc, argv, &mux_oper, NULL);
}

