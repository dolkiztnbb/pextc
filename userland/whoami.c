#include "photon.h"
#include "ulibc.h"

int main(int argc, char* argv[]) {
    int uid = getuid();

    if (uid == 0) {
        print("root\n");
    } else {
        const char* user = u_getenv("USER");
        if (user && user[0] != '\0') {
            print(user);
            print("\n");
        } else {
            print("user\n");
        }
    }

    exit(0);
}