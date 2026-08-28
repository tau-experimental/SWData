#ifndef WAV_IO_H
#define WAV_IO_H

#include <stdio.h>
#include "complex_math.h"

/* Структура заголовка WAV файла (44 байта, стандарт RIFF PCM) */
#pragma pack(push, 1)
typedef struct {
    char     chunk_id[4];       /* "RIFF" */
    unsigned int chunk_size;    /* Размер файла без первых 8 байт */
    char     format[4];         /* "WAVE" */
    char     subchunk1_id[4];   /* "fmt " */
    unsigned int subchunk1_size;/* 16 для PCM */
    unsigned short audio_format;/* 1 для PCM */
    unsigned short num_channels;/* 2 для Stereo (I и Q) */
    unsigned int sample_rate;   /* 8000 Гц */
    unsigned int byte_rate;     /* sample_rate * num_channels * (bits_per_sample/8) */
    unsigned short block_align; /* num_channels * (bits_per_sample/8) */
    unsigned short bits_per_sample; /* 16 бит */
    char     subchunk2_id[4];   /* "data" */
    unsigned int subchunk2_size;/* Размер области данных в байтах */
} wav_header_t;
#pragma pack(pop)

/* Дескриптор открытого WAV-потока */
typedef struct {
    FILE *file;
    wav_header_t header;
    int is_writing;
    unsigned int samples_processed;
} wav_stream_t;

/* Открыть файл для чтения (эмуляция АЦП) */
int wav_open_read(wav_stream_t *stream, const char *filename);

/* Открыть файл для записи (эмуляция ЦАП) */
int wav_open_write(wav_stream_t *stream, const char *filename);

/* Чтение одного комплексного отсчета I/Q */
/* Возвращает 1 при успешном чтении, 0 если файл закончился (EOF) */
int wav_read_sample(wav_stream_t *stream, cplx_f32 *sample);

/* Запись одного комплексного отсчета I/Q */
void wav_write_sample(wav_stream_t *stream, const cplx_f32 *sample);

/* Закрыть файл (для записи — автоматически перезаписывает правильный размер в заголовок) */
void wav_close(wav_stream_t *stream);

#endif /* WAV_IO_H */
