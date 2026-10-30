#include "lilsmoke/keyboard.h"
#include <stdlib.h>
static unsigned checks;
static void check(int valid, const char *name) {
    if (!valid) { fprintf(stderr, "FAIL %s\n", name); exit(1); }
    ++checks;
}
int main(void) {
    char text[32];
    check(lilsmoke_request_text("text", text, sizeof(text), 0, 0) == 0 && !strcmp(text,"hello"), "commit");
    check(lilsmoke_request_text("full", text, 6, 1, 1) == 0 && !strcmp(text,"abcde"), "full capacity");
    check(lilsmoke_request_text("overflow", text, 6, 0, 0) == -1 && !text[0], "overflow rejected");
    check(lilsmoke_request_text("cancel", text, sizeof(text), 0, 0) == 1 && !text[0], "cancel");
    check(lilsmoke_request_text("error", text, sizeof(text), 0, 0) == -1 && !text[0], "child error");
    check(lilsmoke_request_text("text", text, 1, 0, 0) == -1 && errno == EINVAL, "invalid capacity");
    int saved = dup(STDOUT_FILENO);
    close(STDIN_FILENO); close(STDOUT_FILENO);
    int result = lilsmoke_request_text("text", text, sizeof(text), 0, 0);
    dup2(saved, STDOUT_FILENO); close(saved);
    check(result == 0 && !strcmp(text,"hello"), "closed standard descriptors");
    printf("KEYBOARD_API_OK checks=%u\n", checks);
    return 0;
}
