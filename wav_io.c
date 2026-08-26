#include "wav_io.h"
#include <string.h>

FILE* wav_open_write(const char *filename, uint32_t sample_rate) {
    FILE *f = fopen(filename, "wb");
    if (!f) return NULL;

    wav_header_t header;
    // Заполняем текстовые маркеры
    memcpy(header.chunk_id, "RIFF", 4);
    memcpy(header.format, "WAVE", 4);
    memcpy(header.subchunk1_id, "fmt ", 4);
    memcpy(header.subchunk2_id, "data", 4);

    header.subchunk1_size = 16;
    header.audio_format = 1;      // Без сжатия (PCM)
    header.num_channels = 1;      // Моно
    header.sample_rate = sample_rate;
    header.bits_per_sample = 16;  // 16-bit signed
    header.block_align = 2;       // 1 канал * 2 байта
    header.byte_rate = sample_rate * 2;

    // Размеры пока обнуляем, они запишутся при закрытии
    header.chunk_size = 0;
    header.subchunk2_size = 0;

    // Резервируем первые 44 байта в файле
    fwrite(&header, sizeof(wav_header_t), 1, f);
    return f;
}

void wav_close_write(FILE *file) {
    if (!file) return;

    // Узнаем текущий размер файла
    long file_size = ftell(file);
    uint32_t data_size = (uint32_t)(file_size - sizeof(wav_header_t));

    // Считываем старый заголовок, чтобы обновить только размеры
    fseek(file, 0, SEEK_SET);
    wav_header_t header;
    fread(&header, sizeof(wav_header_t), 1, file);

    // Обновляем размеры
    header.subchunk2_size = data_size;
    header.chunk_size = 36 + data_size;

    // Перезаписываем заголовок в начале файла
    fseek(file, 0, SEEK_SET);
    fwrite(&header, sizeof(wav_header_t), 1, file);

    fclose(file);
}
