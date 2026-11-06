









#include "libqhull_r.h"
#include "random_r.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _MSC_VER
#pragma warning( disable : 4706)
#pragma warning( disable : 4996)
#endif




















int qh_argv_to_command(int argc, char *argv[], char* command, int max_size) {
  int i, remaining;
  char *s;
  *command= '\0';

  if (argc) {
    if ((s= strrchr( argv[0], '\\'))
    || (s= strrchr( argv[0], '/')))
        s++;
    else
        s= argv[0];
    if ((int)strlen(s) < max_size)
        strcpy(command, s);
    else
        goto error_argv;
    if ((s= strstr(command, ".EXE"))
    ||  (s= strstr(command, ".exe")))
        *s= '\0';
  }
  for (i=1; i < argc; i++) {
    s= argv[i];
    remaining= max_size - (int)strlen(command) - (int)strlen(s) - 2;
    if (!*s || strchr(s, ' ')) {
      char *t= command + strlen(command);
      remaining -= 2;
      if (remaining < 0) {
        goto error_argv;
      }
      *t++= ' ';
      *t++= '"';
      while (*s) {
        if (*s == '"') {
          if (--remaining < 0)
            goto error_argv;
          *t++= '\\';
        }
        *t++= *s++;
      }
      *t++= '"';
      *t= '\0';
    }else if (remaining < 0) {
      goto error_argv;
    }else
      strcat(command, " ");
      strcat(command, s);
  }
  return 1;

error_argv:
  return 0;
}












int qh_argv_to_command_size(int argc, char *argv[]) {
    unsigned int count= 1;
    int i;
    char *s;

    for (i=0; i<argc; i++){
      count += (int)strlen(argv[i]) + 1;
      if (i>0 && strchr(argv[i], ' ')) {
        count += 2;
        for (s=argv[i]; *s; s++) {
          if (*s == '"') {
            count++;
          }
        }
      }
    }
    return count;
}




















#define qh_rand_a 16807
#define qh_rand_m 2147483647
#define qh_rand_q 127773
#define qh_rand_r 2836

int qh_rand(qhT *qh) {
    int lo, hi, test;
    int seed = qh->last_random;

    hi = seed / qh_rand_q;
    lo = seed % qh_rand_q;
    test = qh_rand_a * lo - qh_rand_r * hi;
    if (test > 0)
        seed= test;
    else
        seed= test + qh_rand_m;
    qh->last_random= seed;


    return seed;
}

void qh_srand(qhT *qh, int seed) {
    if (seed < 1)
        qh->last_random= 1;
    else if (seed >= qh_rand_m)
        qh->last_random= qh_rand_m - 1;
    else
        qh->last_random= seed;
}











realT qh_randomfactor(qhT *qh, realT scale, realT offset) {
    realT randr;

    randr= qh_RANDOMint;
    return randr * scale + offset;
}
















void qh_randommatrix(qhT *qh, realT *buffer, int dim, realT **rows) {
    int i, k;
    realT **rowi, *coord, realr;

    coord= buffer;
    rowi= rows;
    for (i=0; i < dim; i++) {
        *(rowi++)= coord;
        for (k=0; k < dim; k++) {
            realr= qh_RANDOMint;
            *(coord++)= 2.0 * realr/(qh_RANDOMmax+1) - 1.0;
        }
    }
    *rowi= coord;
}










double qh_strtod(const char *s, char **endp) {
  double result;

  result= strtod(s, endp);
  if (s < (*endp) && (*endp)[-1] == ' ')
    (*endp)--;
  return result;
}

int qh_strtol(const char *s, char **endp) {
  int result;

  result= (int) strtol(s, endp, 10);
  if (s< (*endp) && (*endp)[-1] == ' ')
    (*endp)--;
  return result;
}
