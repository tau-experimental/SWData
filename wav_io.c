#include "wav_io.h"
#include <string.h>

FILE* wav_open_write(const char *filename, uint32_t sample_rate) {
    FILE *f = fopen(filename, "wb");
    if (!f) return NULL;
    uint8_t dummy_header[44] = {0};
    fwrite(dummy_header, 1, 44, f);
    return f;
}

void wav_close_write(FILE *file) {
    if (!file) return;

    long file_size = ftell(file);
    uint32_t data_size = (uint32_t)(file_size - 44);
    uint32_t riff_size = 36 + data_size;

    uint32_t sample_rate = 8000;
    uint32_t byte_rate = sample_rate * 4; // 8000 Гц * 2 канала * 2 байта (16 бит стерео)

    uint8_t header[44];
    memcpy(&header[0], "RIFF", 4);

    header[4] = (uint8_t)(riff_size & 0xFF);
    header[5] = (uint8_t)((riff_size >> 8) & 0xFF);
    header[6] = (uint8_t)((riff_size >> 16) & 0xFF);
    header[7] = (uint8_t)((riff_size >> 24) & 0xFF);

    memcpy(&header[8], "WAVE", 4);
    memcpy(&header[12], "fmt ", 4);

    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
    header[20] = 1;  header[21] = 0; // PCM
    header[22] = 2;  header[23] = 0; // ТЕПЕРЬ СТЕРЕО (2 КАНАЛА: I и Q)

    header[24] = (uint8_t)(sample_rate & 0xFF);
    header[25] = (uint8_t)((sample_rate >> 8) & 0xFF);
    header[26] = (uint8_t)((sample_rate >> 16) & 0xFF);
    header[27] = (uint8_t)((sample_rate >> 24) & 0xFF);

    header[28] = (uint8_t)(byte_rate & 0xFF);
    header[29] = (uint8_t)((byte_rate >> 8) & 0xFF);
    header[30] = (uint8_t)((byte_rate >> 16) & 0xFF);
    header[31] = (uint8_t)((byte_rate >> 24) & 0xFF);

    header[32] = 4;  header[33] = 0; // Block Align: 2 канала * 2 байта = 4 байта
    header[34] = 16; header[35] = 0; // 16 бит

    memcpy(&header[36], "data", 4);

    header[40] = (uint8_t)(data_size & 0xFF);
    header[41] = (uint8_t)((data_size >> 8) & 0xFF);
    header[42] = (uint8_t)((data_size >> 16) & 0xFF);
    header[43] = (uint8_t)((data_size >> 24) & 0xFF);

    fseek(file, 0, SEEK_SET);
    fwrite(header, 1, 44, file);
    fclose(file);
}
