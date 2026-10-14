#ifndef PIPE_JOB_H
#define PIPE_JOB_H

#include "photon.h"

// Протокол между /bin/term и /bin/pwait для запуска консольных программ.
//
// term создаёт два пайпа (stdout/stderr ребёнка и stdin ребёнка), переводит
// глобальные дескрипторы STDIN/STDOUT/STDERR на них и запускает pwait. pwait
// делает exec() ребёнка и waitpid() на него, а результат сообщает через
// разделяемый сегмент (shm), который term опрашивает без блокировки.
//
// Почему shm, а не пайп для статуса: таблица дескрипторов в ядре глобальная,
// поэтому закрытие «чужого» дескриптора может задеть другой процесс. shm
// отслеживается по процессам через ref_count и не имеет такой проблемы.

#define JOB_IDLE     0  // сегмент создан, pwait ещё ничего не сделал
#define JOB_SPAWNED  1  // ребёнок запущен, child_pid заполнен
#define JOB_DONE     2  // ребёнок завершился, exit_code заполнен
#define JOB_FAILED   3  // exec() ребёнка не удался

typedef struct {
    volatile int32_t state;
    volatile int32_t child_pid;
    volatile int32_t exit_code;
} pipe_job_status_t;

#endif
