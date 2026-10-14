// /bin/pwait — помощник для /bin/term: запускает консольную программу и
// ждёт её завершения, не блокируя окно терминала.
//
// Использование (только term вызывает напрямую):
//   pwait <shmid> <путь к программе> <argv0> [аргументы...]
//
// term перенаправляет STDIN/STDOUT/STDERR на пайпы до запуска pwait, поэтому
// ребёнок наследует их. Статус pwait пишет в shm-сегмент <shmid>, см.
// ul_includes/pipe_job.h.

#include "photon.h"
#include "ulibc.h"
#include "pipe_job.h"

static int32_t parse_uint(const char* s) {
    int32_t v = 0;
    if (!s) return -1;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
    return v;
}

int main(int argc, char* argv[]) {
    if (argc < 4) return 2;

    int32_t shmid = parse_uint(argv[1]);
    pipe_job_status_t* st = (pipe_job_status_t*)shm_attach(shmid);
    if (!st) return 3;

    // argv[3..] — это argv для ребёнка, argv[0] ребёнка = argv[3]
    int32_t child = exec(argv[2], &argv[3]);
    if (child <= 0) {
        st->exit_code = -1;
        st->state = JOB_FAILED;
        shm_detach(shmid);
        exit(1);
    }

    st->child_pid = child;
    st->state = JOB_SPAWNED;

    int32_t status = 0;
    waitpid(child, &status);

    st->exit_code = status;
    st->state = JOB_DONE;
    shm_detach(shmid);
    exit(0);
    return 0;
}
