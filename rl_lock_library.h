#ifndef RL_LOCK_LIBRARY_H
#define RL_LOCK_LIBRARY_H
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h> // pour les constantes O_*
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "panic.h"

#define NB_OWNERS 10
#define NB_LOCKS 10
#define NB_FILES 256

typedef struct
{
    pid_t proc; /* pid du processus */
    int des;    /* descripteur de fichier */
} owner;

typedef struct
{
    int next_lock;
    int prev_lock;
    off_t starting_offset;
    off_t len;
    short type; /* F_RDLCK F_WRLCK */
    size_t nb_owners;
    owner lock_owners[NB_OWNERS];
} rl_lock;

typedef struct
{
    int first;
    int nb_locks;
    pthread_mutex_t mutex;
    rl_lock lock_table[NB_LOCKS];
} rl_open_file;

typedef struct
{
    int d;
    rl_open_file *f;
} rl_descriptor;

void affiche_rl_desc(rl_descriptor rl_desc);
void affiche_reussite(int res_fcntl, struct flock lck);
rl_descriptor rl_open(const char *path, int oflag, ...);
int rl_close(rl_descriptor lfd);
int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck);
rl_descriptor rl_dup(rl_descriptor lfd);
rl_descriptor rl_dup2(rl_descriptor lfd, int newd);
pid_t rl_fork();
int rl_init_library();

#endif
