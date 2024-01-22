/*	Maryam MUNIM
   Todd CAUËT-MALE*/

#include "rl_lock_library.h"

static struct
{
	int nb_files;
	rl_open_file *tab_open_files[NB_FILES];
} rl_all_files;

static void get_dev_inode(char *shm_buf, char *sem_buf, int fd)
{
	struct stat bstat;
	fstat(fd, &bstat);
	if (shm_buf != NULL)
		sprintf(shm_buf, "/f_%ld_%ld", bstat.st_dev, bstat.st_ino);
	if (sem_buf != NULL)
		sprintf(sem_buf, "/s_%ld_%ld", bstat.st_dev, bstat.st_ino);
}

/* fonction qui représente les segments verouillés du fichier sur le terminal*/
void affiche_rl_desc(rl_descriptor rl_desc)
{
	struct stat buf;
	if (fstat(rl_desc.d, &buf) < 0)
	{
		printf("rl_descriptor inexistant ou a deja été fermé\n");
		return;
	}
	for (int i = 0; i < buf.st_size; i++)
	{
		printf("-");
	}
	printf("\n");
	for (int i = 0; i < rl_desc.f->nb_locks; i++)
	{
		for (int j = 0; j < rl_desc.f->lock_table[i].starting_offset; j++)
		{
			printf(" ");
		}
		for (int j = 0; j < rl_desc.f->lock_table[i].len; j++)
		{
			printf("-");
		}
		printf(" ");

		if (rl_desc.f->lock_table[i].type == F_WRLCK)
			printf("W  ");
		else if (rl_desc.f->lock_table[i].type == F_RDLCK)
			printf("R  ");

		for (int j = 0; j < rl_desc.f->lock_table[i].nb_owners; j++)
		{
			printf("(%d, %d)", rl_desc.f->lock_table[i].lock_owners[j].proc, rl_desc.f->lock_table[i].lock_owners[j].des);
		}
		printf("\n");
	}
	printf("\n");
}

/*fonction qui affiche le résultat de la pose du verrou
par exemple "pid a posé un verrou en lecture" "pid n'a pas réussi à poser un verrou en écriture"*/
void affiche_reussite(int res_fcntl, struct flock lck)
{
	if (lck.l_type == F_UNLCK)
	{
		if (res_fcntl == 0)
			printf("%d a levé un verrou", getpid());
		else
			printf("%d n'a pas réussi à lever un verrou", getpid());
	}
	else if (res_fcntl == 0)
	{
		printf("%d a posé un verrou", getpid());
		if (lck.l_type == 0)
			printf(" en lecture");
		if (lck.l_type == 1)
			printf(" en écriture");
	}
	else
	{
		printf("%d n'a pas réussi à poser un verrou", getpid());
		if (lck.l_type == 0)
			printf(" en lecture");
		if (lck.l_type == 1)
			printf(" en écriture");
	}
	printf(" %ld-%ld\n", lck.l_start, lck.l_start + lck.l_len - 1);
}

static rl_descriptor current;
static void handler(int sig)
{
	rl_close(current);
	_exit(0);
}

static int initialiser_mutex(pthread_mutex_t *pmutex)
{
	pthread_mutexattr_t mutexattr;
	int code;
	code = pthread_mutexattr_init(&mutexattr);
	if (code != 0)
		return code;
	code = pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
	if (code != 0)
		return code;
	code = pthread_mutex_init(pmutex, &mutexattr);
	return code;
}

rl_descriptor rl_open(const char *path, int oflag, ...)
{
	// signal
	struct sigaction act;
	sigemptyset(&act.sa_mask);
	act.sa_handler = handler;
	act.sa_flags = 0;
	if(sigaction(SIGINT, &act, NULL) < 0)
		PANIC_EXIT("sigaction");

	// Ouverture fichier
	rl_descriptor res;
	va_list va;
	va_start(va, oflag);
	int flags = va_arg(va, int);
	int fd = open(path, oflag, flags);
	if (fd < 0)
	{
		res.d = -1;
		return res;
	}

	// On recuperé l'inoeud et numero partition pour le nom du shm
	char shm_name[4 + 2 * sizeof(long unsigned int)];
	char sem_name[4 + 2 * sizeof(long unsigned int)];
	get_dev_inode(shm_name, sem_name, fd);

	// Ouverture/Creation shm
	// shm_unlink(shm_name);
	int shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, S_IWUSR | S_IRUSR);
	if (shm_fd >= 0) // shm créée
	{
		if (ftruncate(shm_fd, sizeof(rl_open_file)) < 0)
			PANIC_EXIT("ftruncate");
		res.f = mmap(0, sizeof(rl_open_file), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
		if (res.f == MAP_FAILED)
			PANIC_EXIT("mmap");

		res.f->first = -2;
		for (int i = 0; i < NB_LOCKS; i++)
		{
			res.f->lock_table[i].nb_owners = 0;
			res.f->lock_table[i].next_lock = -2;
			res.f->lock_table[i].prev_lock = -2;
		}
		if (initialiser_mutex(&res.f->mutex) < 0)
			PANIC_EXIT("mutex");
	}
	else if (shm_fd < 0 && errno == EEXIST) // shm existe deja
	{
		shm_fd = shm_open(shm_name, O_RDWR, S_IWUSR | S_IRUSR);
		if (shm_fd < 0)
			PANIC_EXIT("shm_open");
		res.f = mmap(0, sizeof(rl_open_file), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
		if (res.f == MAP_FAILED)
			PANIC_EXIT("mmap");
	}
	else
		PANIC_EXIT("shm_open");

	rl_all_files.tab_open_files[rl_all_files.nb_files] = res.f;
	rl_all_files.nb_files++;

	res.d = fd;
	current = res;
	return res; // FIX: verif quoi renvoyer quand il y a erreur
}

static owner owner_vide = {.proc = -1, .des = -1};

static rl_open_file open_file_vide = {.first = -2};

static void delete_verrou(rl_descriptor lfd, int i)
{
	int prev = lfd.f->lock_table[i].prev_lock;
	int next = lfd.f->lock_table[i].next_lock;

	if (next != -1 && prev != -1) // verrou pas seul
	{
		// met bien les indices
		lfd.f->lock_table[next].prev_lock = prev;
		lfd.f->lock_table[prev].next_lock = next;
	}
	else if (next == -1 && prev != -1) // dernier verrou
	{
		// met le prev.next = -1
		lfd.f->lock_table[prev].next_lock = -1;
	}
	else if (next != -1 && prev == -1) // premier verrou
	{
		// met le next.prev = -1
		lfd.f->first = next;
		lfd.f->lock_table[next].prev_lock = -1;
	}
	else
	{
		lfd.f->first = -2;
	}
	lfd.f->lock_table[i].next_lock = -2;
	lfd.f->lock_table[i].prev_lock = -2;
	lfd.f->nb_locks--;
}

/* crée un nouveau verrou à partir des infos de lck
   et le pose à la première case disponible du tableau,
   à la fin de la liste chaînée */
static void pose_verrou(rl_descriptor lfd, struct flock lck)
{
	owner lfd_owner = {.proc = getpid(), .des = lfd.d};
	rl_lock lock;
	if (lfd.f->nb_locks == 0)
		lfd.f->first = 0;

	int case_libre = -1;
	int last = -1;
	for (int j = 0; j < NB_LOCKS; j++)
	{
		if (lfd.f->lock_table[j].next_lock == -2 && case_libre == -1)
			case_libre = j;
		if (lfd.f->lock_table[j].next_lock == -1 && last == -1)
			last = j;
	}
	lock.next_lock = -1;
	lock.prev_lock = last;
	lock.starting_offset = lck.l_start;
	lock.len = lck.l_len;
	lock.type = lck.l_type;
	lock.nb_owners = 1;
	lock.lock_owners[0] = lfd_owner;
	if (last != -1)
		lfd.f->lock_table[last].next_lock = case_libre;
	lfd.f->lock_table[case_libre] = lock;
	lfd.f->nb_locks++;
}

/* enlève l'owner j de la liste d'owners du verrou i */
static void delete_owner(rl_descriptor lfd, int i, int j)
{
	// pour le supprimer, j'ai juste mit le dernier à sa place (pour que ça fasse pas de trou)
	int last_owner = lfd.f->lock_table[i].nb_owners - 1;
	lfd.f->lock_table[i].lock_owners[j] = lfd.f->lock_table[i].lock_owners[last_owner];
	lfd.f->lock_table[i].lock_owners[last_owner] = owner_vide;
	lfd.f->lock_table[i].nb_owners--;
}

int rl_close(rl_descriptor lfd)
{
	char shm_name[4 + 2 * sizeof(long unsigned int)];
	char sem_name[4 + 2 * sizeof(long unsigned int)];
	get_dev_inode(shm_name, sem_name, lfd.d);

	if (pthread_mutex_lock(&lfd.f->mutex) < 0)
		PANIC_EXIT("mutex lock");

	// ferme le descripteur
	if (close(lfd.d) == -1)
		PANIC_EXIT("close");

	// verif si lfd_owner est dans la liste d'owners de l'un des verrous de lfd
	int i = lfd.f->first;
	if (i >= 0)
	{
		while (i != -1)
		{
			int n = lfd.f->lock_table[i].nb_owners;
			int tmp;
			for (int j = 0; j < n; j++)
			{
				// si lfd_owner est dans la liste des owners :
				tmp = lfd.f->lock_table[i].next_lock;
				if ((getpid() == lfd.f->lock_table[i].lock_owners[j].proc) &&
					(lfd.d == lfd.f->lock_table[i].lock_owners[j].des))
				{
					// // si c'est le seul owner, on supprime le verrou
					if (lfd.f->lock_table[i].nb_owners == 1)
					{
						delete_verrou(lfd, i);
					}
					// // si ce n'est pas le seul owner, on le retire de la liste des owners
					else
						delete_owner(lfd, i, j);
					break;
				}
			}
			i = tmp;
		}
	}

	if (pthread_mutex_unlock(&lfd.f->mutex) < 0)
		PANIC_EXIT("mutex unlock");

	/* si on a supprimé le dernier verrou sur le fichier,
	   on peut supprimer l'objet mémoire
	   et mettre à jour rl_all_files pour enlever le fichier */
	if (lfd.f->nb_locks == 0)
	{
		// unlink shm
		if (shm_unlink(shm_name) < 0)
			PANIC_EXIT("shm unlink");
		if (munmap(lfd.f, sizeof(rl_open_file)) < 0)
			PANIC_EXIT("munmap");
		// mise à jour rl_all_files
		for (int j = 0; j < rl_all_files.nb_files; j++)
		{
			if (rl_all_files.tab_open_files[j] == lfd.f)
			{
				int last = rl_all_files.nb_files - 1;
				rl_all_files.tab_open_files[j] = rl_all_files.tab_open_files[last];
				rl_all_files.tab_open_files[last] = &open_file_vide;
				rl_all_files.nb_files--;
				break;
			}
		}
	}
	return 0;
}

/*  vérifie s'il y a chevauchement entre verrous
	retourne 0 si non
	1 : début de current overlap avec lck
	2 : fin de current overlap avec lck
	3 : current est compris dans lck
	4 : lck est compris dans current */
static int check_overlap(rl_lock current, struct flock lck)
{
	int flag1 = 0;
	int flag2 = 0;
	if (current.starting_offset == lck.l_start && current.len == lck.l_len) // correspondance parfaite
		return 5;
	if (current.starting_offset <= lck.l_start + lck.l_len - 1 && current.starting_offset >= lck.l_start)
		flag1++;
	if (current.starting_offset + current.len - 1 <= lck.l_start + lck.l_len - 1 && current.starting_offset + current.len - 1 >= lck.l_start)
		flag2 += 2;
	if (current.starting_offset + current.len - 1 > lck.l_start + lck.l_len - 1 && current.starting_offset < lck.l_start)
		return 4;
	if (flag1 && flag2)
		return 3;
	return flag1 + flag2;
}

/* remplace les len = 0 par len = taille jusqu'à fin du fichier */
static void format_size(rl_descriptor lfd, struct flock *lck)
{
	if (lck->l_len == 0)
	{
		struct stat bstat;
		if (fstat(lfd.d, &bstat) < 0)
			PANIC_EXIT("fstat");
		lck->l_len = bstat.st_size - lck->l_start;
	}
}

static void clean(rl_descriptor lfd)
{
	for (int i = 0; i < NB_LOCKS; i++)
	{
		int j = 0;
		while (j != lfd.f->lock_table[i].nb_owners)
		{
			if (kill(lfd.f->lock_table[i].lock_owners[j].proc, 0) < 0)
			{
				if (lfd.f->lock_table[i].nb_owners == 1)
				{
					delete_verrou(lfd, i);
					return;
				}
				else
				{
					delete_owner(lfd, i, j);
				}
			}
			else
				j++;
		}
	}
}

int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck)
{
	owner lfd_owner = {.proc = getpid(), .des = lfd.d};

	if (pthread_mutex_lock(&lfd.f->mutex) < 0)
		PANIC_EXIT("mutex lock");
	format_size(lfd, lck); // enlève les len = 0 par len = fin du fichier
	struct flock save_lock = *lck;

	// sauvegarde de la lock_table
	rl_lock save[NB_LOCKS];
	for (int i = 0; i < NB_LOCKS; i++)
		save[i] = lfd.f->lock_table[i];

	if (cmd == F_SETLK)
	{
		if (lfd.f->nb_locks == NB_LOCKS)
		{
			errno = EAGAIN; // FIX
			if (pthread_mutex_unlock(&lfd.f->mutex) < 0)
				PANIC_EXIT("mutex unlock");
			return -1;
		}

		// SUPPRESION OWNER MORT ET SUPPRESSION VERROU SI PLUS AUCUN OWNER
		clean(lfd);

		int i = lfd.f->first;
		while (i >= 0)
		{
			int tmp_next_lock = lfd.f->lock_table[i].next_lock;
			rl_lock current = lfd.f->lock_table[i];
			int overlap = check_overlap(current, *lck);
			if (overlap)
			{
				// poser en écriture
				if (lck->l_type == F_WRLCK)
				{
					// verif seul autre propriétaire = soit même
					if ((current.nb_owners == 1) &&
						(current.lock_owners[0].proc == lfd_owner.proc &&
						 current.lock_owners[0].des == lfd_owner.des))
					{
						if (current.type == F_WRLCK) // fusion
						{
							switch (overlap)
							{
							case 1:
								delete_verrou(lfd, i);
								lck->l_len = current.starting_offset + current.len;
								break;
							case 2:
								delete_verrou(lfd, i);
								lck->l_len += lck->l_start - current.starting_offset;
								lck->l_start = current.starting_offset;
								break;
							case 3:
								delete_verrou(lfd, i);
								break;
							case 4:
								// si il est compris dans un verrou déjà existant, c'est qu'il n'y avait pas de conflits
								if (pthread_mutex_unlock(&lfd.f->mutex) < 0)
									PANIC_EXIT("mutex unlock");
								return 0;
							case 5:
								if (pthread_mutex_unlock(&lfd.f->mutex) < 0)
									PANIC_EXIT("mutex unlock"); // ERROR
								return 0;
							default:
								break;
							}
						}
						else if (current.type == F_RDLCK) // division
						{
							struct flock tmp;
							switch (overlap)
							{
							case 1:
								lfd.f->lock_table[i].len = current.starting_offset + current.len - (lck->l_start + lck->l_len);
								lfd.f->lock_table[i].starting_offset = lck->l_start + lck->l_len;
								break;
							case 2:
								lfd.f->lock_table[i].len = lck->l_start - lfd.f->lock_table[i].starting_offset;
								break;
							case 3:
								delete_verrou(lfd, i);
								break;
							case 4:
								// pose un nouveau
								tmp.l_type = lfd.f->lock_table[i].type;
								tmp.l_start = lck->l_start + lck->l_len;
								tmp.l_len = lfd.f->lock_table[i].starting_offset + lfd.f->lock_table[i].len - tmp.l_start;
								pose_verrou(lfd, tmp);
								// réduit
								lfd.f->lock_table[i].len = lck->l_start - lfd.f->lock_table[i].starting_offset;
								break;
							case 5:
								lfd.f->lock_table[i].type = F_WRLCK; // promouvoit
								if (pthread_mutex_unlock(&lfd.f->mutex) < 0)
									PANIC_EXIT("mutex unlock"); // ERROR
								return 0;
							default:
								break;
							}
						}
					}
					else // quelqu'un d'autre en lecture ou en écriture = incompatible
					{
						// on récupère la sauvegarde
						for (int i = 0; i < NB_LOCKS; i++)
							lfd.f->lock_table[i] = save[i];
						errno = EAGAIN;
						if (pthread_mutex_unlock(&lfd.f->mutex) < 0)
							PANIC_EXIT("mutex unlock"); // ERROR
						return -1;
					}
				}
				else if (lck->l_type == F_RDLCK)
				{
					if (current.type == F_WRLCK)
					{ // si quelqu'un en écriture : incompatible
						for (int i = 0; i < NB_LOCKS; i++)
							lfd.f->lock_table[i] = save[i];
						errno = EAGAIN;
						if (pthread_mutex_unlock(&lfd.f->mutex) < 0)
							PANIC_EXIT("mutex unlock"); // ERROR
						return -1;
					}
					if (current.type == F_RDLCK)
					{
						int is_an_owner = 0;
						for (int j = 0; j < current.nb_owners; j++)
						{
							if (current.lock_owners[j].proc == lfd_owner.proc &&
								current.lock_owners[j].des == lfd_owner.des)
							{
								is_an_owner = 1;
								// agrandit lck
								switch (overlap)
								{
								case 1:
									lck->l_len = current.starting_offset + current.len - lck->l_start;
									break;
								case 2:
									lck->l_len += lck->l_start - current.starting_offset;
									lck->l_start = current.starting_offset;
									break;
								case 4:
									// si il est compris dans un verrou déjà existant, c'est qu'il n'y avait pas de conflits
									if (pthread_mutex_unlock(&lfd.f->mutex) < 0)
										PANIC_EXIT("mutex unlock"); // ERROR
									return 0;
								case 5:
									if (pthread_mutex_unlock(&lfd.f->mutex) < 0)
										PANIC_EXIT("mutex unlock"); // ERROR
									return 0;
								default:
									break;
								}
								if (current.nb_owners > 1) // plusieurs owner
									delete_owner(lfd, i, j);
								else // seul
									delete_verrou(lfd, i);
								// break;
							}
						}
						if (overlap == 5 && is_an_owner == 0) // si coïncidence parfaite mais qu'on est pas dans les owners
						{
							// on s'ajoute dans les owners et on s'arête (car pas d'autres conflits)
							lfd.f->lock_table[i].lock_owners[current.nb_owners] = lfd_owner;
							lfd.f->lock_table[i].nb_owners++;
							if (pthread_mutex_unlock(&lfd.f->mutex) < 0)
								PANIC_EXIT("mutex unlock"); // ERROR
							return 0;
						}
					}
				}
				else if (lck->l_type == F_UNLCK)
				{
					struct flock new_lck;
					struct flock new_lck2;

					switch (overlap)
					{
					case 1:
						for (int j = 0; j < current.nb_owners; j++)
						{
							// si c'est un owner
							if (current.lock_owners[j].proc == lfd_owner.proc &&
								current.lock_owners[j].des == lfd_owner.des)
							{
								if (current.nb_owners == 1)
								{
									// réduit le verrou
									lfd.f->lock_table[i].len -= lck->l_start + lck->l_len - current.starting_offset;
									lfd.f->lock_table[i].starting_offset = lck->l_start + lck->l_len;
									break;
								}
								// plusieurs owners
								else
								{
									delete_owner(lfd, i, j);
									// LEN meme calcul que if (calcul du len)
									new_lck.l_start = lck->l_start + lck->l_len;
									new_lck.l_len = current.starting_offset + current.len - new_lck.l_start;
									new_lck.l_type = current.type;
									pose_verrou(lfd, new_lck);
									break;
								}
							}
						}
						break;
					case 2:
						for (int j = 0; j < current.nb_owners; j++)
						{
							// si c'est un owner
							if (current.lock_owners[j].proc == lfd_owner.proc &&
								current.lock_owners[j].des == lfd_owner.des)
							{
								if (current.nb_owners == 1)
								{
									// réduit le verrou
									lfd.f->lock_table[i].len = lck->l_start - current.starting_offset;
									break;
								}
								// plusieurs owners
								else
								{
									delete_owner(lfd, i, j);
									new_lck.l_start = current.starting_offset;
									new_lck.l_len = lck->l_start - current.starting_offset;
									new_lck.l_type = current.type;
									pose_verrou(lfd, new_lck);
									break;
								}
							}
						}
						break;
					case 3:
						for (int j = 0; j < current.nb_owners; j++)
						{
							if (current.lock_owners[j].proc == lfd_owner.proc &&
								current.lock_owners[j].des == lfd_owner.des)
							{
								if (current.nb_owners == 1)
									delete_verrou(lfd, i);
								else
									delete_owner(lfd, i, j);
								break;
							}
						}
						break;
					case 4:
						for (int j = 0; j < current.nb_owners; j++)
						{
							if (current.lock_owners[j].proc == lfd_owner.proc &&
								current.lock_owners[j].des == lfd_owner.des)
							{
								if (current.nb_owners == 1)
								{
									// réduit le current
									lfd.f->lock_table[i].len = lck->l_start - current.starting_offset;
									// ajoute un nouveau
									new_lck.l_start = lck->l_start + lck->l_len;
									new_lck.l_len = current.starting_offset + current.len - new_lck.l_start;
									new_lck.l_type = current.type;
									pose_verrou(lfd, new_lck);
								}
								else
								{
									// delete des owners
									delete_owner(lfd, i, j);
									// crée 2 nouveaux verrous séparés
									new_lck.l_start = lck->l_start + lck->l_len;
									new_lck.l_len = current.starting_offset + current.len - new_lck.l_start;
									new_lck.l_type = current.type;
									pose_verrou(lfd, new_lck);

									new_lck2.l_start = current.starting_offset;
									new_lck2.l_len = lck->l_start - current.starting_offset;
									new_lck2.l_type = current.type;
									pose_verrou(lfd, new_lck2);
								}
								break;
							}
						}
						break;
					case 5:
						for (int j = 0; j < current.nb_owners; j++)
						{
							if (current.lock_owners[j].proc == lfd_owner.proc &&
								current.lock_owners[j].des == lfd_owner.des)
							{
								if (current.nb_owners == 1)
								{
									delete_verrou(lfd, i);
								}
								else
								{
									delete_owner(lfd, i, j);
								}
								break;
							}
						}
					default:
						break;
					}
				}
			}
			i = tmp_next_lock;
		}
		if (lck->l_type != F_UNLCK)
			pose_verrou(lfd, *lck);
	}
	*lck = save_lock;

	if (pthread_mutex_unlock(&lfd.f->mutex) < 0)
		PANIC_EXIT("mutex unlock"); // ERROR
	return 0;
}

static void dup_occ(rl_open_file *f, owner lfd_owner, int newd)
{
	int current = f->first;
	if (current >= 0) // On verifie qu'il existe au moins un verrou
	{
		while (current != -1)
		{
			int n = f->lock_table[current].nb_owners;
			for (int i = 0; i < n; i++)
			{
				owner ow = f->lock_table[current].lock_owners[i];
				if (lfd_owner.proc == ow.proc && lfd_owner.des == ow.des)
				{
					owner new_owner = {.proc = getpid(), .des = newd};
					f->lock_table[current].lock_owners[f->lock_table[current].nb_owners] = new_owner;
					f->lock_table[current].nb_owners++;
				}
			}
			current = f->lock_table[current].next_lock;
		}
	}
}

rl_descriptor rl_dup(rl_descriptor lfd)
{
	owner lfd_owner = {.proc = getpid(), .des = lfd.d};
	int newd = dup(lfd.d); // ERROR
	if (newd < 0)
		PANIC_EXIT("dup");
	dup_occ(lfd.f, lfd_owner, newd);

	rl_descriptor new_rl_descriptor = {.d = newd, .f = lfd.f};
	return new_rl_descriptor;
}

rl_descriptor rl_dup2(rl_descriptor lfd, int newd)
{
	owner lfd_owner = {.proc = getpid(), .des = lfd.d};
	if (dup2(lfd.d, newd) < 0)
		PANIC_EXIT("dup2");

	dup_occ(lfd.f, lfd_owner, newd);

	rl_descriptor new_rl_descriptor = {.d = newd, .f = lfd.f};
	return new_rl_descriptor;
}

pid_t rl_fork()
{
	pid_t parent = getpid();
	pid_t pid = fork();
	if (pid > 0)
		return pid;
	else if (pid < 0)
		PANIC_EXIT("fork");
	printf("naissance de %d par fork\n", getpid());

	// parcours fichiers
	for (int k = 0; k < rl_all_files.nb_files; k++)
	{
		rl_open_file *file = rl_all_files.tab_open_files[k];

		if (pthread_mutex_lock(&file->mutex) < 0)
			PANIC_EXIT("mutex lock"); // ERROR
		int i = file->first;
		// parcours verrous
		while (i >= 0)
		{
			// parcours owners
			int nb_owners = file->lock_table[i].nb_owners;
			for (int j = 0; j < nb_owners; j++)
			{
				if (file->lock_table[i].lock_owners[j].proc == parent)
				{
					// ajout de l'enfant dans les owners
					owner new_owner = {.proc = getpid(), .des = file->lock_table[i].lock_owners[j].des};
					file->lock_table[i].lock_owners[nb_owners] = new_owner;
					file->lock_table[i].nb_owners++;
				}
			}
			i = file->lock_table[i].next_lock;
		}
		if (pthread_mutex_unlock(&file->mutex) < 0)
			PANIC_EXIT("mutex unlock"); // ERROR
	}

	return 0;
}

int rl_init_library()
{
	rl_all_files.nb_files = 0;
	for (int i = 0; i < NB_FILES; i++)
	{
		rl_all_files.tab_open_files[i] = NULL;
	}
	return 0;
}
