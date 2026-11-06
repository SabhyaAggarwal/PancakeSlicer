


















extern "C" {
    void    qh_exit(int exitcode);
}

#include <stdexcept>
#include <stdlib.h>










void qh_exit(int exitcode) {
    exitcode= exitcode;
    throw std::runtime_error("QH10003 Qhull error.  See stderr or errfile.");
}
















void qh_fprintf_stderr(int msgcode, const char *fmt, ... ) {
    va_list args;

    va_start(args, fmt);
    if(msgcode)
      fprintf(stderr, "QH%.4d ", msgcode);
    vfprintf(stderr, fmt, args);
    va_end(args);
}











void qh_free(void *mem) {
    free(mem);
}










void *qh_malloc(size_t size) {
    return malloc(size);
}
