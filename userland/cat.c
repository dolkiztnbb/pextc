#include "photon.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print("Usage: cat <file>\n");
        return 1;
    }
    
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], READ);
        if (fd < 0) {
            print("cat: "); print(argv[i]); print(": No such file or directory\n");
            continue;
        }
        
        char buf[256];
        int n;
        // Читаем файл кусками, пока он не закончится
        while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            print(buf);
        }
        close(fd);
    }
    print("\n");
    return 0;
}