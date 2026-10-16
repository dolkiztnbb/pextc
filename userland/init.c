#ifdef __LINUX_COMPAT_MODE__
    #include "lcl.h"
#else
    #include "photon.h"
#endif

int main(int argc, char* argv[]) { // <--- Меняем void на классические аргументы

    // Обязательно NULL-терминатор в конце массива аргументов!
    char* args[2] = {
        "/bin/psh",
        NULL
    };
    char* args_wm[2] = {
        "/bin/wm",
        NULL
    };

    int pid2 = exec("/bin/wm", args_wm);
    waitpid(pid2, NULL);

    setuid(1000);
    while(1){
        // WM can switch VGA modes; always return to a neutral text palette.
        set_io_color(0x0, 0x7, 0x0, 0x7);
        int pid = exec("/bin/psh", args);
        
        if (pid > 0) {
            // init засыпает и ждет, пока psh не завершится
            waitpid(pid, NULL);
        } else {
            // Если файла /bin/psh нет на диске
            print("init: failed to launch /bin/psh\n");
        }

        char letter;
        set_io_color(0x0,0x7,0x0,0x7);

        print("fallback to iksh? [y/N]: ");
        read(STDIN,&letter,1);
        print("\n");
        if (letter=='y'){
            return 0;
        }
    }
}