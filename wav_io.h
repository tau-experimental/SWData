#ifndef WAV_IO_H
#define WAV_IO_H

#include <stdint.h>
#include <stdio.h>

// Структура стандартного заголовка WAV (IEEE FLOAT)
typedef struct {
    char     chunk_id[4];      // "RIFF"
    uint32_t chunk_size;
    char     format[4];        // "WAVE"
    char     fmt_id[4];        // "fmt "
    uint32_t fmt_size;         // 16 для PCM, 18 для Float
    uint16_t audio_format;     // 3 = IEEE Float
    uint16_t num_channels;     // 2 = Стерео (I/Q)
    uint32_t sample_rate;      // 8000
    uint32_t byte_rate;        // sample_rate * num_channels * (bits_per_sample/8)
    uint16_t block_align;      // num_channels * (bits_per_sample/8)
    uint16_t bits_per_sample;  // 32
    char     data_id[4];       // "data"
    uint32_t data_size;
} wav_header_t;


// Создать WAV файл со стерео-потоком Float32
FILE* wav_open_write(const char* filename, uint32_t sample_rate);
void wav_write_sample(FILE* f, float i_ch, float q_ch);
void wav_close(FILE* f);

#endif // WAV_IO_H
