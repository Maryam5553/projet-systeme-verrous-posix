#include "rl_lock_library.h"

int main()
{
    rl_init_library();
    rl_descriptor rl_desc = rl_open("equipe", O_RDWR);
    struct flock lck;
    lck.l_start = 3;
    lck.l_len = 5;
    lck.l_type = F_WRLCK;
    if (rl_fcntl(rl_desc, F_SETLK, &lck) < 0)
        printf("non\n");
    affiche_rl_desc(rl_desc);
    // rl_close(rl_desc);
}
