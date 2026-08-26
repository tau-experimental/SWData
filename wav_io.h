#ifndef WAV_IO_H
#define WAV_IO_H

#include <stdint.h>
#include <stdio.h>

// Открывает файл, резервируя первые 44 байта
FILE* wav_open_write(const char *filename, uint32_t sample_rate);

// Считает размер данных, формирует побайтовый RIFF заголовок и закрывает файл
void  wav_close_write(FILE *file);

#endif // WAV_IO_H
