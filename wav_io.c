#include "wav_io.h"

int wav_writer_open(wav_writer_t *writer, const char *filename, uint32_t sample_rate) {
    writer->file = fopen(filename, "wb");
    if (!writer->file) return 0;

    // Резервируем место под стандартный заголовок WAV (44 байта)
    uint8_t dummy_header[44] = {0};
    fwrite(dummy_header, 1, 44, writer->file);
    writer->data_size = 0;
    return 1;
}

void wav_writer_write_sample(wav_writer_t *writer, float sample_i, float sample_q) {
    // Жесткое ограничение (clipping), чтобы не выйти за пределы int16_t
    if (sample_i > 1.0f) sample_i = 1.0f;
    if (sample_i < -1.0f) sample_i = -1.0f;
    if (sample_q > 1.0f) sample_q = 1.0f;
    if (sample_q < -1.0f) sample_q = -1.0f;

    // Конвертация float [-1.0, 1.0] в signed int16
    int16_t out_i = (int16_t)(sample_i * 32767.0f);
    int16_t out_q = (int16_t)(sample_q * 32767.0f);

    fwrite(&out_i, sizeof(int16_t), 1, writer->file);
    fwrite(&out_q, sizeof(int16_t), 1, writer->file);

    writer->data_size += 2 * sizeof(int16_t); // 2 канала по 2 байта
}

void wav_writer_close(wav_writer_t *writer) {
    if (!writer->file) return;

    // Возвращаемся в начало и перезаписываем реальный заголовок RIFF
    fseek(writer->file, 0, SEEK_SET);

    uint32_t total_file_size = writer->data_size + 36;
    uint32_t sample_rate = 8000; // Наша константа FS
    uint32_t byte_rate = sample_rate * 2 * sizeof(int16_t); // Стерео 16-бит
    uint16_t block_align = 2 * sizeof(int16_t);

    // Сборка заголовка
    fwrite("RIFF", 1, 4, writer->file);
    fwrite(&total_file_size, 4, 1, writer->file);
    fwrite("WAVEfmt ", 1, 8, writer->file);

    uint32_t subchunk1_size = 16; // PCM заголовок
    uint16_t audio_format = 1;    // Без сжатия (PCM)
    uint16_t num_channels = 2;    // Стерео (I/Q)

    fwrite(&subchunk1_size, 4, 1, writer->file);
    fwrite(&audio_format, 2, 1, writer->file);
    fwrite(&num_channels, 2, 1, writer->file);
    fwrite(&sample_rate, 4, 1, writer->file);
    fwrite(&byte_rate, 4, 1, writer->file);
    fwrite(&block_align, 2, 1, writer->file);

    uint16_t bits_per_sample = 16;
    fwrite(&bits_per_sample, 2, 1, writer->file);

    fwrite("data", 1, 4, writer->file);
    fwrite(&writer->data_size, 4, 1, writer->file);

    fclose(writer->file);
    writer->file = NULL;
}
