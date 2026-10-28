/*
 * Echojam Audio FX Plugin
 *
 * Port of Airwindows Echojam (MIT) into the Move Anything audio_fx_api_v2.
 * Original: https://github.com/airwindows/airwindows
 * Copyright (c) Airwindows — Chris Johnson
 *
 * Echo that is secretly a failed reverb: 16 delay lines, Householder mixing,
 * Bezier undersampling for Speed. 17 Select programs, no tempo grid.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "audio_fx_api_v1.h"

#define SAMPLE_RATE 44100.0f

/* Max delay lengths from Airwindows Echojam.h (buffers are these + 5). */
#define D4A 6654
#define D4B 7450
#define D4C 6998
#define D4D 6650
#define D4E 7136
#define D4F 7198
#define D4G 7253
#define D4H 7221
#define D4I 7355
#define D4J 4838
#define D4K 7069
#define D4L 7175
#define D4M 7307
#define D4N 6819
#define D4O 7104
#define D4P 7443
#define CAP(n) ((n) + 5)
#define POOL_SAMPLES (CAP(D4A) + CAP(D4B) + CAP(D4C) + CAP(D4D) + \
                      CAP(D4E) + CAP(D4F) + CAP(D4G) + CAP(D4H) + \
                      CAP(D4I) + CAP(D4J) + CAP(D4K) + CAP(D4L) + \
                      CAP(D4M) + CAP(D4N) + CAP(D4O) + CAP(D4P))

enum {
    bez_AL, bez_AR, bez_BL, bez_BR, bez_CL, bez_CR,
    bez_SampL, bez_SampR, bez_cycle, bez_total
};

#define AUDIO_FX_API_VERSION_2 2

typedef struct audio_fx_api_v2 {
    uint32_t api_version;
    void* (*create_instance)(const char *module_dir, const char *config_json);
    void (*destroy_instance)(void *instance);
    void (*process_block)(void *instance, int16_t *audio_inout, int frames);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
} audio_fx_api_v2_t;

static const host_api_v1_t *g_host = NULL;

static void plugin_log(const char *msg) {
    if (g_host && g_host->log) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[echojam] %s", msg);
        g_host->log(buf);
    }
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

typedef struct {
    char module_dir[256];

    int param_select;     /* 0..16 */
    float param_regen;    /* 0..1  (Airwindows B) */
    float param_speed;    /* 0..1  (Airwindows C) */
    float param_mix;      /* 0..1  (Airwindows D) */

    float *poolL;
    float *poolR;
    float *lA, *lB, *lC, *lD, *lE, *lF, *lG, *lH;
    float *lI, *lJ, *lK, *lL, *lM, *lN, *lO, *lP;
    float *rA, *rB, *rC, *rD, *rE, *rF, *rG, *rH;
    float *rI, *rJ, *rK, *rL, *rM, *rN, *rO, *rP;

    int cLA, cLB, cLC, cLD, cLE, cLF, cLG, cLH;
    int cLI, cLJ, cLK, cLL, cLM, cLN, cLO, cLP;
    int cRA, cRB, cRC, cRD, cRE, cRF, cRG, cRH;
    int cRI, cRJ, cRK, cRL, cRM, cRN, cRO, cRP;

    float fAL, fBL, fCL, fDL;
    float hAL, hBL, hCL, hDL, hEL;
    float fDR, fHR, fLR, fPR;
    float hAR, hBR, hCR, hDR, hER;

    float bez[bez_total];

    int shortA, shortB, shortC, shortD, shortE, shortF, shortG, shortH;
    int shortI, shortJ, shortK, shortL, shortM, shortN, shortO, shortP;
    int prevclearcoat;

    uint32_t fpdL;
    uint32_t fpdR;
    int initialized;
} echojam_instance_t;

static void assign_line_ptrs(echojam_instance_t *s) {
    float *p = s->poolL;
    s->lA = p; p += CAP(D4A);
    s->lB = p; p += CAP(D4B);
    s->lC = p; p += CAP(D4C);
    s->lD = p; p += CAP(D4D);
    s->lE = p; p += CAP(D4E);
    s->lF = p; p += CAP(D4F);
    s->lG = p; p += CAP(D4G);
    s->lH = p; p += CAP(D4H);
    s->lI = p; p += CAP(D4I);
    s->lJ = p; p += CAP(D4J);
    s->lK = p; p += CAP(D4K);
    s->lL = p; p += CAP(D4L);
    s->lM = p; p += CAP(D4M);
    s->lN = p; p += CAP(D4N);
    s->lO = p; p += CAP(D4O);
    s->lP = p;

    p = s->poolR;
    s->rA = p; p += CAP(D4A);
    s->rB = p; p += CAP(D4B);
    s->rC = p; p += CAP(D4C);
    s->rD = p; p += CAP(D4D);
    s->rE = p; p += CAP(D4E);
    s->rF = p; p += CAP(D4F);
    s->rG = p; p += CAP(D4G);
    s->rH = p; p += CAP(D4H);
    s->rI = p; p += CAP(D4I);
    s->rJ = p; p += CAP(D4J);
    s->rK = p; p += CAP(D4K);
    s->rL = p; p += CAP(D4L);
    s->rM = p; p += CAP(D4M);
    s->rN = p; p += CAP(D4N);
    s->rO = p; p += CAP(D4O);
    s->rP = p;
}

static void reset_counters(echojam_instance_t *s) {
    s->cLA = s->cLB = s->cLC = s->cLD = s->cLE = s->cLF = s->cLG = s->cLH = 1;
    s->cLI = s->cLJ = s->cLK = s->cLL = s->cLM = s->cLN = s->cLO = s->cLP = 1;
    s->cRA = s->cRB = s->cRC = s->cRD = s->cRE = s->cRF = s->cRG = s->cRH = 1;
    s->cRI = s->cRJ = s->cRK = s->cRL = s->cRM = s->cRN = s->cRO = s->cRP = 1;
}

static void set_shorts(echojam_instance_t *s, int clearcoat) {
    switch (clearcoat) {
    case 0:
        s->shortA = 17; s->shortB = 19; s->shortC = 23; s->shortD = 29;
        s->shortE = 19; s->shortF = 106; s->shortG = 41; s->shortH = 6361;
        s->shortI = 47; s->shortJ = 53; s->shortK = 59; s->shortL = 61;
        s->shortM = 67; s->shortN = 5824; s->shortO = 6257; s->shortP = 653;
        break;
    case 1:
        s->shortA = 17; s->shortB = 7450; s->shortC = 1692; s->shortD = 29;
        s->shortE = 64; s->shortF = 37; s->shortG = 41; s->shortH = 43;
        s->shortI = 47; s->shortJ = 4838; s->shortK = 776; s->shortL = 61;
        s->shortM = 67; s->shortN = 6307; s->shortO = 1158; s->shortP = 79;
        break;
    case 2:
        s->shortA = 17; s->shortB = 19; s->shortC = 23; s->shortD = 29;
        s->shortE = 6111; s->shortF = 6237; s->shortG = 41; s->shortH = 5973;
        s->shortI = 47; s->shortJ = 53; s->shortK = 59; s->shortL = 768;
        s->shortM = 67; s->shortN = 71; s->shortO = 73; s->shortP = 6822;
        break;
    case 3:
        s->shortA = 17; s->shortB = 19; s->shortC = 23; s->shortD = 29;
        s->shortE = 5841; s->shortF = 37; s->shortG = 41; s->shortH = 43;
        s->shortI = 794; s->shortJ = 53; s->shortK = 59; s->shortL = 6160;
        s->shortM = 7230; s->shortN = 71; s->shortO = 73; s->shortP = 6473;
        break;
    case 4:
        s->shortA = 17; s->shortB = 3; s->shortC = 6854; s->shortD = 6650;
        s->shortE = 31; s->shortF = 6854; s->shortG = 41; s->shortH = 765;
        s->shortI = 47; s->shortJ = 53; s->shortK = 59; s->shortL = 61;
        s->shortM = 67; s->shortN = 71; s->shortO = 73; s->shortP = 809;
        break;
    case 5:
        s->shortA = 756; s->shortB = 19; s->shortC = 6998; s->shortD = 29;
        s->shortE = 31; s->shortF = 37; s->shortG = 6808; s->shortH = 43;
        s->shortI = 47; s->shortJ = 53; s->shortK = 59; s->shortL = 61;
        s->shortM = 6978; s->shortN = 71; s->shortO = 269; s->shortP = 79;
        break;
    case 6:
        s->shortA = 5824; s->shortB = 652; s->shortC = 739; s->shortD = 6086;
        s->shortE = 31; s->shortF = 37; s->shortG = 41; s->shortH = 6852;
        s->shortI = 47; s->shortJ = 75; s->shortK = 59; s->shortL = 61;
        s->shortM = 67; s->shortN = 71; s->shortO = 73; s->shortP = 6589;
        break;
    case 7:
        s->shortA = 17; s->shortB = 19; s->shortC = 6906; s->shortD = 29;
        s->shortE = 6361; s->shortF = 37; s->shortG = 6098; s->shortH = 43;
        s->shortI = 47; s->shortJ = 53; s->shortK = 59; s->shortL = 765;
        s->shortM = 67; s->shortN = 71; s->shortO = 6133; s->shortP = 79;
        break;
    case 8:
        s->shortA = 6617; s->shortB = 19; s->shortC = 315; s->shortD = 29;
        s->shortE = 31; s->shortF = 5982; s->shortG = 41; s->shortH = 676;
        s->shortI = 47; s->shortJ = 53; s->shortK = 7069; s->shortL = 61;
        s->shortM = 67; s->shortN = 71; s->shortO = 73; s->shortP = 79;
        break;
    case 9:
        s->shortA = 17; s->shortB = 19; s->shortC = 6171; s->shortD = 29;
        s->shortE = 31; s->shortF = 37; s->shortG = 41; s->shortH = 43;
        s->shortI = 7355; s->shortJ = 53; s->shortK = 906; s->shortL = 61;
        s->shortM = 7307; s->shortN = 71; s->shortO = 1911; s->shortP = 79;
        break;
    case 10:
        s->shortA = 744; s->shortB = 649; s->shortC = 433; s->shortD = 29;
        s->shortE = 31; s->shortF = 37; s->shortG = 41; s->shortH = 6974;
        s->shortI = 47; s->shortJ = 53; s->shortK = 59; s->shortL = 61;
        s->shortM = 7203; s->shortN = 71; s->shortO = 6478; s->shortP = 79;
        break;
    case 11:
        s->shortA = 848; s->shortB = 19; s->shortC = 23; s->shortD = 29;
        s->shortE = 7136; s->shortF = 7187; s->shortG = 41; s->shortH = 43;
        s->shortI = 726; s->shortJ = 53; s->shortK = 59; s->shortL = 7175;
        s->shortM = 67; s->shortN = 71; s->shortO = 73; s->shortP = 79;
        break;
    case 12:
        s->shortA = 17; s->shortB = 19; s->shortC = 23; s->shortD = 6428;
        s->shortE = 31; s->shortF = 7198; s->shortG = 7253; s->shortH = 7087;
        s->shortI = 47; s->shortJ = 53; s->shortK = 59; s->shortL = 878;
        s->shortM = 67; s->shortN = 71; s->shortO = 13; s->shortP = 79;
        break;
    case 13:
        s->shortA = 962; s->shortB = 19; s->shortC = 23; s->shortD = 29;
        s->shortE = 31; s->shortF = 37; s->shortG = 41; s->shortH = 7221;
        s->shortI = 47; s->shortJ = 53; s->shortK = 59; s->shortL = 6437;
        s->shortM = 67; s->shortN = 71; s->shortO = 7104; s->shortP = 79;
        break;
    case 14:
        s->shortA = 17; s->shortB = 19; s->shortC = 4995; s->shortD = 29;
        s->shortE = 20; s->shortF = 37; s->shortG = 41; s->shortH = 5153;
        s->shortI = 47; s->shortJ = 53; s->shortK = 5325; s->shortL = 61;
        s->shortM = 67; s->shortN = 71; s->shortO = 958; s->shortP = 7443;
        break;
    case 15:
        s->shortA = 17; s->shortB = 19; s->shortC = 23; s->shortD = 5994;
        s->shortE = 5965; s->shortF = 37; s->shortG = 41; s->shortH = 6324;
        s->shortI = 47; s->shortJ = 664; s->shortK = 59; s->shortL = 61;
        s->shortM = 5693; s->shortN = 71; s->shortO = 73; s->shortP = 79;
        break;
    case 16:
    default:
        s->shortA = 6654; s->shortB = 19; s->shortC = 23; s->shortD = 29;
        s->shortE = 31; s->shortF = 37; s->shortG = 41; s->shortH = 43;
        s->shortI = 47; s->shortJ = 21; s->shortK = 59; s->shortL = 740;
        s->shortM = 67; s->shortN = 6819; s->shortO = 6662; s->shortP = 79;
        break;
    }
}

static void apply_select(echojam_instance_t *s, int clearcoat) {
    memset(s->poolL, 0, POOL_SAMPLES * sizeof(float));
    memset(s->poolR, 0, POOL_SAMPLES * sizeof(float));
    reset_counters(s);
    set_shorts(s, clearcoat);
    s->prevclearcoat = clearcoat;
}

static void* v2_create_instance(const char *module_dir, const char *config_json) {
    plugin_log("Creating instance");
    (void)config_json;

    echojam_instance_t *s = (echojam_instance_t *)calloc(1, sizeof(echojam_instance_t));
    if (!s) {
        plugin_log("Failed to allocate instance");
        return NULL;
    }

    if (module_dir) {
        strncpy(s->module_dir, module_dir, sizeof(s->module_dir) - 1);
    }

    s->param_select = 8; /* Airwindows default A=0.5 → (int)(0.5*16.999)=8 */
    s->param_regen = 0.5f;
    s->param_speed = 0.5f;
    s->param_mix = 0.5f;

    s->poolL = (float *)calloc(POOL_SAMPLES, sizeof(float));
    s->poolR = (float *)calloc(POOL_SAMPLES, sizeof(float));
    if (!s->poolL || !s->poolR) {
        plugin_log("Failed to allocate delay buffers");
        free(s->poolL);
        free(s->poolR);
        free(s);
        return NULL;
    }
    assign_line_ptrs(s);
    reset_counters(s);
    set_shorts(s, 0);
    s->prevclearcoat = -1;

    s->bez[bez_cycle] = 1.0f;
    s->fpdL = 1u;
    s->fpdR = 1u;
    s->initialized = 1;
    plugin_log("Instance created");
    return s;
}

static void v2_destroy_instance(void *instance) {
    echojam_instance_t *s = (echojam_instance_t *)instance;
    if (!s) return;
    plugin_log("Destroying instance");
    free(s->poolL);
    free(s->poolR);
    free(s);
}

static void echojam_tick(echojam_instance_t *s, float inputSampleL, float inputSampleR,
                         float regen) {
    s->lA[s->cLA] = s->fAL * regen + inputSampleL;
    s->lB[s->cLB] = s->fBL * regen + inputSampleL;
    s->lC[s->cLC] = s->fCL * regen + inputSampleL;
    s->lD[s->cLD] = s->fDL * regen + inputSampleL;
    s->rD[s->cRD] = s->fDR * regen + inputSampleR;
    s->rH[s->cRH] = s->fHR * regen + inputSampleR;
    s->rL[s->cRL] = s->fLR * regen + inputSampleR;
    s->rP[s->cRP] = s->fPR * regen + inputSampleR;
    s->cLA++; s->cLB++; s->cLC++; s->cLD++;
    s->cRD++; s->cRH++; s->cRL++; s->cRP++;
    if (s->cLA > s->shortA) s->cLA = 0;
    if (s->cLB > s->shortB) s->cLB = 0;
    if (s->cLC > s->shortC) s->cLC = 0;
    if (s->cLD > s->shortD) s->cLD = 0;
    if (s->cRD > s->shortD) s->cRD = 0;
    if (s->cRH > s->shortH) s->cRH = 0;
    if (s->cRL > s->shortL) s->cRL = 0;
    if (s->cRP > s->shortP) s->cRP = 0;
    s->hAL = s->lA[s->cLA]; s->hBL = s->lB[s->cLB]; s->hCL = s->lC[s->cLC]; s->hDL = s->lD[s->cLD];
    s->hAR = s->rD[s->cRD]; s->hBR = s->rH[s->cRH]; s->hCR = s->rL[s->cRL]; s->hDR = s->rP[s->cRP];
    s->hEL = -(s->hAL + s->hBL + s->hCL + s->hDL);
    s->hER = -(s->hAR + s->hBR + s->hCR + s->hDR);
    s->lE[s->cLE] = s->hAL * 2.0f + s->hEL;
    s->lF[s->cLF] = s->hBL * 2.0f + s->hEL;
    s->lG[s->cLG] = s->hCL * 2.0f + s->hEL;
    s->lH[s->cLH] = s->hDL * 2.0f + s->hEL;
    s->rC[s->cRC] = s->hAR * 2.0f + s->hER;
    s->rG[s->cRG] = s->hBR * 2.0f + s->hER;
    s->rK[s->cRK] = s->hCR * 2.0f + s->hER;
    s->rO[s->cRO] = s->hDR * 2.0f + s->hER;
    s->cLE++; s->cLF++; s->cLG++; s->cLH++;
    s->cRC++; s->cRG++; s->cRK++; s->cRO++;
    if (s->cLE > s->shortE) s->cLE = 0;
    if (s->cLF > s->shortF) s->cLF = 0;
    if (s->cLG > s->shortG) s->cLG = 0;
    if (s->cLH > s->shortH) s->cLH = 0;
    if (s->cRC > s->shortC) s->cRC = 0;
    if (s->cRG > s->shortG) s->cRG = 0;
    if (s->cRK > s->shortK) s->cRK = 0;
    if (s->cRO > s->shortO) s->cRO = 0;
    s->hAL = s->lE[s->cLE]; s->hBL = s->lF[s->cLF]; s->hCL = s->lG[s->cLG]; s->hDL = s->lH[s->cLH];
    s->hAR = s->rC[s->cRC]; s->hBR = s->rG[s->cRG]; s->hCR = s->rK[s->cRK]; s->hDR = s->rO[s->cRO];
    s->hEL = -(s->hAL + s->hBL + s->hCL + s->hDL);
    s->hER = -(s->hAR + s->hBR + s->hCR + s->hDR);
    s->lI[s->cLI] = s->hAL * 2.0f + s->hEL;
    s->lJ[s->cLJ] = s->hBL * 2.0f + s->hEL;
    s->lK[s->cLK] = s->hCL * 2.0f + s->hEL;
    s->lL[s->cLL] = s->hDL * 2.0f + s->hEL;
    s->rB[s->cRB] = s->hAR * 2.0f + s->hER;
    s->rF[s->cRF] = s->hBR * 2.0f + s->hER;
    s->rJ[s->cRJ] = s->hCR * 2.0f + s->hER;
    s->rN[s->cRN] = s->hDR * 2.0f + s->hER;
    s->cLI++; s->cLJ++; s->cLK++; s->cLL++;
    s->cRB++; s->cRF++; s->cRJ++; s->cRN++;
    if (s->cLI > s->shortI) s->cLI = 0;
    if (s->cLJ > s->shortJ) s->cLJ = 0;
    if (s->cLK > s->shortK) s->cLK = 0;
    if (s->cLL > s->shortL) s->cLL = 0;
    if (s->cRB > s->shortB) s->cRB = 0;
    if (s->cRF > s->shortF) s->cRF = 0;
    if (s->cRJ > s->shortJ) s->cRJ = 0;
    if (s->cRN > s->shortN) s->cRN = 0;
    s->hAL = s->lI[s->cLI]; s->hBL = s->lJ[s->cLJ]; s->hCL = s->lK[s->cLK]; s->hDL = s->lL[s->cLL];
    s->hAR = s->rB[s->cRB]; s->hBR = s->rF[s->cRF]; s->hCR = s->rJ[s->cRJ]; s->hDR = s->rN[s->cRN];
    s->hEL = -(s->hAL + s->hBL + s->hCL + s->hDL);
    s->hER = -(s->hAR + s->hBR + s->hCR + s->hDR);
    s->lM[s->cLM] = s->hAL * 2.0f + s->hEL;
    s->lN[s->cLN] = s->hBL * 2.0f + s->hEL;
    s->lO[s->cLO] = s->hCL * 2.0f + s->hEL;
    s->lP[s->cLP] = s->hDL * 2.0f + s->hEL;
    s->rA[s->cRA] = s->hAR * 2.0f + s->hER;
    s->rE[s->cRE] = s->hBR * 2.0f + s->hER;
    s->rI[s->cRI] = s->hCR * 2.0f + s->hER;
    s->rM[s->cRM] = s->hDR * 2.0f + s->hER;
    s->cLM++; s->cLN++; s->cLO++; s->cLP++;
    s->cRA++; s->cRE++; s->cRI++; s->cRM++;
    if (s->cLM > s->shortM) s->cLM = 0;
    if (s->cLN > s->shortN) s->cLN = 0;
    if (s->cLO > s->shortO) s->cLO = 0;
    if (s->cLP > s->shortP) s->cLP = 0;
    if (s->cRA > s->shortA) s->cRA = 0;
    if (s->cRE > s->shortE) s->cRE = 0;
    if (s->cRI > s->shortI) s->cRI = 0;
    if (s->cRM > s->shortM) s->cRM = 0;
    s->hAL = s->lM[s->cLM]; s->hBL = s->lN[s->cLN]; s->hCL = s->lO[s->cLO]; s->hDL = s->lP[s->cLP];
    s->hAR = s->rA[s->cRA]; s->hBR = s->rE[s->cRE]; s->hCR = s->rI[s->cRI]; s->hDR = s->rM[s->cRM];
    s->hEL = -(s->hAL + s->hBL + s->hCL + s->hDL);
    s->hER = -(s->hAR + s->hBR + s->hCR + s->hDR);
    s->fAL = s->hAR * 2.0f + s->hER;
    s->fBL = s->hBR * 2.0f + s->hER;
    s->fCL = s->hCR * 2.0f + s->hER;
    s->fDL = s->hDR * 2.0f + s->hER;
    s->fDR = s->hAL * 2.0f + s->hEL;
    s->fHR = s->hBL * 2.0f + s->hEL;
    s->fLR = s->hCL * 2.0f + s->hEL;
    s->fPR = s->hDL * 2.0f + s->hEL;
}

static void v2_process_block(void *instance, int16_t *audio_inout, int frames) {
    echojam_instance_t *s = (echojam_instance_t *)instance;
    if (!s || !s->initialized) return;

    int clearcoat = s->param_select;
    if (clearcoat < 0) clearcoat = 0;
    if (clearcoat > 16) clearcoat = 16;
    if (clearcoat != s->prevclearcoat) {
        apply_select(s, clearcoat);
    }

    float regen = (1.0f - powf(1.0f - s->param_regen, 2.0f)) * 0.0625f;
    float overallscale = SAMPLE_RATE / 44100.0f;
    float derez = powf(s->param_speed, overallscale);
    derez = clampf(derez, 0.0001f, 1.0f);
    int bezFraction = (int)(1.0f / derez);
    if (bezFraction < 1) bezFraction = 1;
    float bezTrim = (float)bezFraction / (float)(bezFraction + 1);
    derez = 1.0f / (float)bezFraction;
    bezTrim = 1.0f - (derez * bezTrim);
    float wet = 1.0f - powf(1.0f - s->param_mix, 2.0f);

    for (int i = 0; i < frames; i++) {
        float inputSampleL = audio_inout[i * 2] / 32768.0f;
        float inputSampleR = audio_inout[i * 2 + 1] / 32768.0f;
        if (fabsf(inputSampleL) < 1.18e-23f) inputSampleL = (float)s->fpdL * 1.18e-17f;
        if (fabsf(inputSampleR) < 1.18e-23f) inputSampleR = (float)s->fpdR * 1.18e-17f;
        float drySampleL = inputSampleL;
        float drySampleR = inputSampleR;

        s->bez[bez_cycle] += derez;
        s->bez[bez_SampL] += inputSampleL * derez;
        s->bez[bez_SampR] += inputSampleR * derez;
        if (s->bez[bez_cycle] > 1.0f) {
            s->bez[bez_cycle] = 0.0f;
            echojam_tick(s, s->bez[bez_SampL], s->bez[bez_SampR], regen);
            s->bez[bez_CL] = s->bez[bez_BL];
            s->bez[bez_BL] = s->bez[bez_AL];
            s->bez[bez_AL] = (s->hAL + s->hBL + s->hCL + s->hDL) * 0.0625f;
            s->bez[bez_SampL] = 0.0f;
            s->bez[bez_CR] = s->bez[bez_BR];
            s->bez[bez_BR] = s->bez[bez_AR];
            s->bez[bez_AR] = (s->hAR + s->hBR + s->hCR + s->hDR) * 0.0625f;
            s->bez[bez_SampR] = 0.0f;
        }

        float X = s->bez[bez_cycle] * bezTrim;
        float CBL = (s->bez[bez_CL] * (1.0f - X)) + (s->bez[bez_BL] * X);
        float CBR = (s->bez[bez_CR] * (1.0f - X)) + (s->bez[bez_BR] * X);
        float BAL = (s->bez[bez_BL] * (1.0f - X)) + (s->bez[bez_AL] * X);
        float BAR = (s->bez[bez_BR] * (1.0f - X)) + (s->bez[bez_AR] * X);
        inputSampleL = (s->bez[bez_BL] + (CBL * (1.0f - X)) + (BAL * X)) * 0.125f;
        inputSampleR = (s->bez[bez_BR] + (CBR * (1.0f - X)) + (BAR * X)) * 0.125f;

        inputSampleL = inputSampleL * wet + drySampleL * (1.0f - wet);
        inputSampleR = inputSampleR * wet + drySampleR * (1.0f - wet);

        /* TPDF-style xorshift advance (Airwindows fpd), no float dither — we land on int16. */
        s->fpdL ^= s->fpdL << 13; s->fpdL ^= s->fpdL >> 17; s->fpdL ^= s->fpdL << 5;
        s->fpdR ^= s->fpdR << 13; s->fpdR ^= s->fpdR >> 17; s->fpdR ^= s->fpdR << 5;

        if (inputSampleL > 1.0f) inputSampleL = 1.0f;
        if (inputSampleL < -1.0f) inputSampleL = -1.0f;
        if (inputSampleR > 1.0f) inputSampleR = 1.0f;
        if (inputSampleR < -1.0f) inputSampleR = -1.0f;

        audio_inout[i * 2] = (int16_t)(inputSampleL * 32767.0f);
        audio_inout[i * 2 + 1] = (int16_t)(inputSampleR * 32767.0f);
    }
}

static int json_get_number(const char *json, const char *key, float *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *pos = strstr(json, search);
    if (!pos) return -1;
    pos += strlen(search);
    while (*pos == ' ') pos++;
    *out = (float)atof(pos);
    return 0;
}

static int parse_select(const char *val) {
    int sel = atoi(val);
    if (sel < 0) sel = 0;
    if (sel > 16) sel = 16;
    return sel;
}

static void v2_set_param(void *instance, const char *key, const char *val) {
    echojam_instance_t *s = (echojam_instance_t *)instance;
    if (!s) return;

    if (strcmp(key, "state") == 0) {
        float v;
        if (json_get_number(val, "select", &v) == 0) {
            int sel = (int)(v + 0.5f);
            if (sel < 0) sel = 0;
            if (sel > 16) sel = 16;
            s->param_select = sel;
        }
        if (json_get_number(val, "regen", &v) == 0) s->param_regen = clampf(v, 0.0f, 1.0f);
        if (json_get_number(val, "speed", &v) == 0) s->param_speed = clampf(v, 0.0f, 1.0f);
        if (json_get_number(val, "mix", &v) == 0) s->param_mix = clampf(v, 0.0f, 1.0f);
        return;
    }

    if (strcmp(key, "select") == 0) {
        s->param_select = parse_select(val);
        return;
    }

    float v = clampf((float)atof(val), 0.0f, 1.0f);
    if (strcmp(key, "regen") == 0) {
        s->param_regen = v;
    } else if (strcmp(key, "speed") == 0) {
        s->param_speed = v;
    } else if (strcmp(key, "mix") == 0) {
        s->param_mix = v;
    }
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    echojam_instance_t *s = (echojam_instance_t *)instance;
    if (!s) return -1;

    if (strcmp(key, "select") == 0) {
        return snprintf(buf, buf_len, "%d", s->param_select);
    }
    if (strcmp(key, "regen") == 0) {
        return snprintf(buf, buf_len, "%.2f", s->param_regen);
    }
    if (strcmp(key, "speed") == 0) {
        return snprintf(buf, buf_len, "%.2f", s->param_speed);
    }
    if (strcmp(key, "mix") == 0) {
        return snprintf(buf, buf_len, "%.2f", s->param_mix);
    }
    if (strcmp(key, "name") == 0) {
        return snprintf(buf, buf_len, "Echojam");
    }
    if (strcmp(key, "state") == 0) {
        return snprintf(buf, buf_len,
            "{\"select\":%d,\"regen\":%.4f,\"speed\":%.4f,\"mix\":%.4f}",
            s->param_select, s->param_regen, s->param_speed, s->param_mix);
    }
    if (strcmp(key, "ui_hierarchy") == 0) {
        const char *hierarchy =
            "{"
            "\"modes\":null,"
            "\"levels\":{"
                "\"root\":{"
                    "\"children\":null,"
                    "\"knobs\":[\"select\",\"regen\",\"speed\",\"mix\"],"
                    "\"params\":[\"select\",\"regen\",\"speed\",\"mix\"]"
                "}"
            "}"
            "}";
        int len = (int)strlen(hierarchy);
        if (len < buf_len) {
            strcpy(buf, hierarchy);
            return len;
        }
        return -1;
    }
    if (strcmp(key, "chain_params") == 0) {
        const char *params_json =
            "["
            "{\"key\":\"select\",\"name\":\"Select\",\"type\":\"int\",\"min\":0,\"max\":16,\"step\":1},"
            "{\"key\":\"regen\",\"name\":\"Regen\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"speed\",\"name\":\"Speed\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01},"
            "{\"key\":\"mix\",\"name\":\"Dry/Wet\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01}"
            "]";
        int len = (int)strlen(params_json);
        if (len < buf_len) {
            strcpy(buf, params_json);
            return len;
        }
        return -1;
    }
    return -1;
}

static audio_fx_api_v2_t g_fx_api_v2;

audio_fx_api_v2_t* move_audio_fx_init_v2(const host_api_v1_t *host) {
    g_host = host;
    memset(&g_fx_api_v2, 0, sizeof(g_fx_api_v2));
    g_fx_api_v2.api_version = AUDIO_FX_API_VERSION_2;
    g_fx_api_v2.create_instance = v2_create_instance;
    g_fx_api_v2.destroy_instance = v2_destroy_instance;
    g_fx_api_v2.process_block = v2_process_block;
    g_fx_api_v2.set_param = v2_set_param;
    g_fx_api_v2.get_param = v2_get_param;
    plugin_log("V2 API initialized");
    return &g_fx_api_v2;
}
