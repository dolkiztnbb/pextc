#include "photon.h"

int main(int argc, char* argv[]) {
    char buf[256];
    if (getcwd(buf, sizeof(buf)) == 0) {
        print(buf);
        print("\n");
    } else {
        print("pwd: error getting current directory\n");
    }
    return 0;
}