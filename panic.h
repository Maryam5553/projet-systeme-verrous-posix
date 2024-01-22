#ifndef PANIC_H
#define PANIC_H
#define PANIC_EXIT(msg)                                                   \
    do                                                                    \
    {                                                                     \
        fprintf(stderr,                                                   \
                "\n %d %s : error \"%s\" in file %s in line %d\n",        \
                (int)getpid(), msg, strerror(errno), __FILE__, __LINE__); \
        exit(1);                                                          \
    } while (0)
#endif /* panic_h */
