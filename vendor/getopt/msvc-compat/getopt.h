#ifndef CTX_SHIM_GETOPT_H
#define CTX_SHIM_GETOPT_H

#define no_argument 0
#define required_argument 1
#define optional_argument 2

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;
extern int optreset;

#ifdef __cplusplus
extern "C" {
#endif

int getopt(int argc, char * const *argv, const char *options);
int getopt_long(int argc, char * const *argv, const char *options,
    const struct option *long_options, int *index);

#ifdef __cplusplus
}
#endif

#endif
