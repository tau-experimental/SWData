#include "wav_io.h"
#include <string.h>

FILE* wav_open_write(const char* filename, uint32_t sample_rate) {
    FILE* f = fopen(filename, "wb");
    if (!f) return NULL;

    wav_header_t header;
    // Заполняем сигнатуры
    unsigned char riff[4] = {'R','I','F','F'};
    unsigned char wave[4] = {'W','A','V','E'};
    unsigned char fmt [4] = {'f','m','t',' '};
    unsigned char data[4] = {'d','a','t','a'};

    for(int i=0; i<4; i++) {
        header.chunk_id[i] = riff[i];
        header.format[i] = wave[i];
        header.fmt_id[i] = fmt[i];
        header.data_id[i] = data[i];
    }

    header.chunk_size = 0; // Перепишем при закрытии
    header.fmt_size = 16;
    header.audio_format = 3; // 3 означает IEEE FLOAT
    header.num_channels = 2; // Стерео (I = Левый, Q = Правый)
    header.sample_rate = sample_rate;
    header.bits_per_sample = 32;
    header.block_align = 2 * 4; // 2 канала * 4 байта
    header.byte_rate = sample_rate * header.block_align;
    header.data_size = 0;  // Перепишем при закрытии

    fwrite(&header, sizeof(wav_header_t), 1, f);
    return f;
}

// Записать один комплексный отсчет (I/Q) в WAV
void wav_write_sample(FILE* f, float i_ch, float q_ch) {
    fwrite(&i_ch, sizeof(float), 1, f);
    fwrite(&q_ch, sizeof(float), 1, f);
}

// Корректно закрыть файл и обновить размеры данных в заголовке
void wav_close(FILE* f) {
    if (!f) return;
    long file_size = ftell(f);
    uint32_t data_size = (uint32_t)(file_size - 44);
    uint32_t chunk_size = (uint32_t)(file_size - 8);

    fseek(f, 4, SEEK_SET);
    fwrite(&chunk_size, sizeof(uint32_t), 1, f);
    fseek(f, 40, SEEK_SET);
    fwrite(&data_size, sizeof(uint32_t), 1, f);
    fclose(f);
}
