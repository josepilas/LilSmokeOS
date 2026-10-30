#include "GameHistory.h"

int main(int argc, char **argv) {
    if (argc < 6) return 2;
    lilsmoke::RecentGame game{argv[1], argv[2], argv[3]};
    lilsmoke::GameHistory history;
    std::vector<std::string> arguments;
    for (int i = 5; i < argc; ++i) arguments.emplace_back(argv[i]);
    return lilsmoke::runGameSession(game, argv[4], arguments, history);
}
