#ifndef WAV_IO_H
#define WAV_IO_H

#include <stdint.h>
#include <stdio.h>

#pragma pack(push, 1)
typedef struct {
    // RIFF заголовок
    char     chunk_id[4];       // "RIFF"
    uint32_t chunk_size;        // 36 + subchunk2_size
    char     format[4];         // "WAVE"

    // Subchunk 1: Спецификация аудио-формата
    char     subchunk1_id[4];   // "fmt "
    uint32_t subchunk1_size;    // 16 для PCM
    uint16_t audio_format;      // 1 для PCM
    uint16_t num_channels;      // 1 (Моно)
    uint32_t sample_rate;       // Частота дискретизации (8000)
    uint32_t byte_rate;         // sample_rate * num_channels * bits_per_sample/8
    uint16_t block_align;       // num_channels * bits_per_sample/8
    uint16_t bits_per_sample;   // 16 бит

    // Subchunk 2: Данные
    char     subchunk2_id[4];   // "data"
    uint32_t subchunk2_size;    // Количество байт аудио-данных
} wav_header_t;
#pragma pack(pop)

// Функции для работы с файлами
FILE* wav_open_write(const char *filename, uint32_t sample_rate);
void  wav_close_write(FILE *file);

#endif // WAV_IO_H
