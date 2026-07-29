// 骰子猜拳游戏 — 入口
#include <iostream>
#include "game/game.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <kmodel> [conf] [nms] [debug]" << std::endl;
        return -1;
    }
    GameController game(argc, argv);
    if (!game.init()) {
        std::cerr << "Init failed!" << std::endl;
        return -1;
    }
    game.run();
    return 0;
}
