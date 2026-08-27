#ifndef WAV_WRITER_H
#define WAV_WRITER_H

#include <stdio.h>
#include <stdint.h>

typedef struct {
    FILE *file;
    uint32_t data_size;
} wav_writer_t;

int wav_writer_open(wav_writer_t *writer, const char *filename, uint32_t sample_rate);
void wav_writer_write_sample(wav_writer_t *writer, float sample_i, float sample_q);
void wav_writer_close(wav_writer_t *writer);

#endif /* WAV_WRITER_H */
