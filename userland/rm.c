#include "photon.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print("Usage: rm <file/dir>\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) != 0) {
            print("rm: cannot remove "); print(argv[i]); print("\n");
        }
    }
    return 0;
}