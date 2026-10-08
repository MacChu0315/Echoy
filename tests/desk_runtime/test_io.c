#define main echoy_desk_program_main
#include "../../projects/echoy/pc/desk_main.c"
#undef main
#include <assert.h>
int main(void)
{
    struct desk d={.silent=true,.capture="/dev/full"};
    atomic_store(&d.stopping,true);audio_thread(&d);
    assert(atomic_load(&d.failed));
    pid_t pid=fork();assert(pid>=0);
    if(!pid){raise(SIGSTOP);for(;;)pause();}
    int status;assert(waitpid(pid,&status,WUNTRACED)==pid&&WIFSTOPPED(status));
    int64_t start=now_us(NULL);assert(stop_audio_process(pid));assert(now_us(NULL)-start<2500000);
    puts("PASS capture errors are reported and stopped audio children are reaped");
    return 0;
}
