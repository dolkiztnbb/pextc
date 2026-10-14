#include "photon.h"
#include "ulibc.h"

int main(int argc, char* argv[]) {
    // Проверяем, кто запустил программу через наш новый сисколл
    if (getuid() != 0) {
        print("reboot: Need to be root (try sudo reboot)\n");
        exit(1);
    }

    print("System is going down for reboot NOW!\n");
    sleep(500); // Даем 500мс, чтобы текст успел отрисоваться

    reboot();

    // Если мы дошли сюда — перезагрузка не удалась
    print("reboot: operation failed\n");
    exit(1);
}