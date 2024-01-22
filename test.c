#include "rl_lock_library.h"

int main()
{
    rl_init_library();
    rl_descriptor lfd1 = rl_open("equipe", O_RDWR);

    printf("**** TEST POSE VERROU LECTURE ****\n\n");

    /* pose d'un premier verrou*/
    struct flock lck;
    lck.l_start = 3;
    lck.l_len = 8;
    lck.l_type = F_RDLCK;

    affiche_reussite(rl_fcntl(lfd1, F_SETLK, &lck), lck);
    affiche_rl_desc(lfd1);

    /* pose d'un deuxième verrou plus loin */
    lck.l_start = 15;
    lck.l_len = 8;
    lck.l_type = F_RDLCK;

    affiche_reussite(rl_fcntl(lfd1, F_SETLK, &lck), lck);
    affiche_rl_desc(lfd1);

    /* pose troisième au milieu : fusion */
    lck.l_start = 9;
    lck.l_len = 8;
    lck.l_type = F_RDLCK;

    affiche_reussite(rl_fcntl(lfd1, F_SETLK, &lck), lck);
    affiche_rl_desc(lfd1);

    printf("\n**** TEST UNLOCK ****\n\n");
    /* unlock au milieu d'un segment */
    lck.l_start = 8;
    lck.l_len = 8;
    lck.l_type = F_UNLCK;

    affiche_reussite(rl_fcntl(lfd1, F_SETLK, &lck), lck);
    affiche_rl_desc(lfd1);

    /* unlock la totalité du fichier */
    lck.l_start = 0;
    lck.l_len = 0;
    lck.l_type = F_UNLCK;

    affiche_reussite(rl_fcntl(lfd1, F_SETLK, &lck), lck);
    affiche_rl_desc(lfd1);

    printf("\n**** TEST WRITE ****\n\n");
    /* pose d'un verrou en lecture */
    lck.l_start = 3;
    lck.l_len = 15;
    lck.l_type = F_RDLCK;

    affiche_reussite(rl_fcntl(lfd1, F_SETLK, &lck), lck);
    affiche_rl_desc(lfd1);

    /* pose d'un verrou en écriture : le verrou en lecture se scinde */
    lck.l_start = 12;
    lck.l_len = 12;
    lck.l_type = F_WRLCK;

    affiche_reussite(rl_fcntl(lfd1, F_SETLK, &lck), lck);
    affiche_rl_desc(lfd1);

    lck.l_start = 7;
    lck.l_len = 10;
    lck.l_type = F_UNLCK;

    affiche_reussite(rl_fcntl(lfd1, F_SETLK, &lck), lck);
    affiche_rl_desc(lfd1);

    printf("\n**** TEST FORK ****\n\n");
    pid_t pid = rl_fork();
    usleep(1000);

    if (pid > 0)
        affiche_rl_desc(lfd1);

    usleep(1000);

    if (pid == 0)
    {
        printf("\n**** CONFLITS VERROUS ****\n\n");
        lck.l_start = 16;
        lck.l_len = 23;
        lck.l_type = F_RDLCK;
        affiche_reussite(rl_fcntl(lfd1, F_SETLK, &lck), lck);
        affiche_rl_desc(lfd1);
    }
    usleep(1000);

    if (pid > 0)
    {
        lck.l_start = 3;
        lck.l_len = 10;
        lck.l_type = F_WRLCK;
        affiche_reussite(rl_fcntl(lfd1, F_SETLK, &lck), lck);
        affiche_rl_desc(lfd1);
    }
    usleep(5000);

    /* quand le fils fait close, cela lève tous ses verrous */
    if (pid == 0)
    {
        printf("\n**** TEST CLOSE ****\n\n");
        rl_close(lfd1);
        printf("le fils a fait close\n");
        return 0;
    }

    usleep(5000);
    affiche_rl_desc(lfd1);

    /* fermer un descripteur ne lève les verrous associés qu'au descripteur */

    rl_descriptor lfd2 = rl_open("equipe", O_RDWR);
    printf("ouverture d'un 2e descripteur de fichier\n");

    lck.l_start = 3;
    lck.l_len = 4;
    lck.l_type = F_RDLCK;
    affiche_reussite(rl_fcntl(lfd2, F_SETLK, &lck), lck); // pose verrou sur le 2e descripteur
    affiche_rl_desc(lfd1);

    rl_close(lfd2);
    printf("fermeture du 2e descripteur\n");
    affiche_rl_desc(lfd1);

    printf("\n**** TEST DUP ****\n\n");

    printf("dup\n");
    lfd2 = rl_dup(lfd1);
    affiche_rl_desc(lfd1);

    printf("dup2\n");
    int fd = open("equipe", O_RDWR);
    rl_descriptor lfd3 = rl_dup2(lfd1, fd);
    affiche_rl_desc(lfd1);

    rl_close(lfd1);
    rl_close(lfd2);
    rl_close(lfd3);
    return 0;
}
