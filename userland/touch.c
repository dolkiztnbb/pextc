#include "photon.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print("Usage: touch <file>\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], CREATE | WRITE);
        if (fd >= 0) close(fd);
        else {
            print("touch: cannot create "); print(argv[i]); print("\n");
        }
    }
    return 0;
}