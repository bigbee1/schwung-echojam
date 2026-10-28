#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/dsp/echojam.c"

static void test_log(const char *msg) {
    (void)msg;
}

int main(void) {
    host_api_v1_t host = {0};
    host.log = test_log;

    audio_fx_api_v2_t *api = move_audio_fx_init_v2(&host);
    if (!api) {
        fprintf(stderr, "failed to initialize API\n");
        return 1;
    }

    void *instance = api->create_instance(NULL, "{}");
    if (!instance) {
        fprintf(stderr, "failed to create instance\n");
        return 1;
    }

    char buf[256];
    if (api->get_param(instance, "name", buf, sizeof(buf)) < 0 || strcmp(buf, "Echojam") != 0) {
        fprintf(stderr, "expected name Echojam, got '%s'\n", buf);
        api->destroy_instance(instance);
        return 1;
    }

    api->set_param(instance, "select", "0");
    api->set_param(instance, "regen", "0.0");
    api->set_param(instance, "speed", "1.0");
    api->set_param(instance, "mix", "1.0");

    const int frames = 8000;
    int16_t *audio = calloc((size_t)frames * 2, sizeof(int16_t));
    if (!audio) {
        fprintf(stderr, "failed to allocate buffer\n");
        api->destroy_instance(instance);
        return 1;
    }

    audio[0] = 30000;
    audio[1] = 30000;
    api->process_block(instance, audio, frames);

    int peak = 0;
    int peak_at = -1;
    for (int i = 50; i < frames; i++) {
        int l = abs(audio[i * 2]);
        int r = abs(audio[i * 2 + 1]);
        int m = l > r ? l : r;
        if (m > peak) {
            peak = m;
            peak_at = i;
        }
    }

    if (peak < 100) {
        fprintf(stderr, "expected wet echo energy after impulse, peak=%d at %d\n", peak, peak_at);
        free(audio);
        api->destroy_instance(instance);
        return 1;
    }

    /* Mix 0 should pass dry through. */
    api->set_param(instance, "mix", "0.0");
    memset(audio, 0, (size_t)frames * 2 * sizeof(int16_t));
    audio[0] = 20000;
    audio[1] = -20000;
    api->process_block(instance, audio, 128);
    if (abs(audio[0] - 20000) > 2 || abs(audio[1] + 20000) > 2) {
        fprintf(stderr, "mix=0 should pass dry, got L=%d R=%d\n", audio[0], audio[1]);
        free(audio);
        api->destroy_instance(instance);
        return 1;
    }

    free(audio);
    api->destroy_instance(instance);
    return 0;
}
