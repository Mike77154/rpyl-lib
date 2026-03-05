#include <stdio.h>
#include <stdlib.h>

#include "rpyl.h"

static void cmd_show(RpylContext* ctx, const char** args, int argc) {
    (void)ctx;
    if (argc < 1) return;
    printf("[Show] %s\n", args[0]);
}

static void cmd_draw(RpylContext* ctx, const char** args, int argc) {
    (void)ctx;
    if (argc < 3) return;
    printf("[Draw] x=%d y=%d size=%d\n", atoi(args[0]), atoi(args[1]), atoi(args[2]));
}

int main(int argc, char** argv) {
    const char* script = (argc > 1) ? argv[1] : "script.rpy";
    const char* entry  = (argc > 2) ? argv[2] : "Game Start";
    int frames          = (argc > 3) ? atoi(argv[3]) : 3;
    int i;

    RpylContext* rpyl = rpyl_create();
    if (!rpyl) {
        fprintf(stderr, "failed to create rpyl\n");
        return 1;
    }

    rpyl_register_command(rpyl, "Show", cmd_show);
    rpyl_register_command(rpyl, "Draw", cmd_draw);

    if (!rpyl_load_file(rpyl, script)) {
        fprintf(stderr, "failed to load script: %s\n", script);
        rpyl_destroy(rpyl);
        return 1;
    }

    for (i = 0; i < frames; i++) {
        printf("--- frame %d ---\n", i);
        rpyl_run(rpyl, entry);
    }

    rpyl_destroy(rpyl);
    return 0;
}
