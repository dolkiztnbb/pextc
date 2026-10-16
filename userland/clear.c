#include "photon.h"

int main(int argc, char* argv[]) {
    // Стандартная ANSI-команда очистки работает и в текстовой консоли,
    // и в GUI-терминале, который интерпретирует CSI 2 J и CSI H.
    print("\033[2J\033[H");
    return 0;
}
