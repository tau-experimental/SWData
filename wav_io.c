#include "wav_io.h"
#include <string.h>

int wav_open_read(wav_stream_t *stream, const char *filename) {
    stream->file = fopen(filename, "rb");
    if (!stream->file) return 0;

    if (fread(&stream->header, sizeof(wav_header_t), 1, stream->file) != 1) {
        fclose(stream->file);
        return 0;
    }

    /* Проверка формата: строго 16-бит, стерео, 8000 Гц, PCM */
    if (memcmp(stream->header.chunk_id, "RIFF", 4) != 0 ||
        memcmp(stream->header.format, "WAVE", 4) != 0 ||
        stream->header.audio_format != 1 ||
        stream->header.num_channels != 2 ||
        stream->header.sample_rate != 8000 ||
        stream->header.bits_per_sample != 16) {
        fclose(stream->file);
        return 0;
    }

    stream->is_writing = 0;
    stream->samples_processed = 0;
    return 1;
}

int wav_open_write(wav_stream_t *stream, const char *filename) {
    stream->file = fopen(filename, "wb");
    if (!stream->file) return 0;

    /* Заполняем базовый шаблон заголовка (размеры обновим при закрытии) */
    memset(&stream->header, 0, sizeof(wav_header_t));
    memcpy(stream->header.chunk_id, "RIFF", 4);
    memcpy(stream->header.format, "WAVE", 4);
    memcpy(stream->header.subchunk1_id, "fmt ", 4);
    stream->header.subchunk1_size = 16;
    stream->header.audio_format = 1;
    stream->header.num_channels = 2;
    stream->header.sample_rate = 8000;
    stream->header.bits_per_sample = 16;
    stream->header.block_align = 2 * (16 / 8); /* 4 байта на стереоотсчет */
    stream->header.byte_rate = 8000 * stream->header.block_align;
    memcpy(stream->header.subchunk2_id, "data", 4);

    /* Записываем пустой заголовок как заглушку */
    if (fwrite(&stream->header, sizeof(wav_header_t), 1, stream->file) != 1) {
        fclose(stream->file);
        return 0;
    }

    stream->is_writing = 1;
    stream->samples_processed = 0;
    return 1;
}

int wav_read_sample(wav_stream_t *stream, cplx_f32 *sample) {
    short buffer[2]; /* [0] = Left (I), [1] = Right (Q) */

    if (fread(buffer, sizeof(short), 2, stream->file) != 2) {
        return 0; /* Конец файла или ошибка */
    }

    /* Масштабируем из [-32768..32767] в [-1.0f..1.0f] */
    sample->re = (float)buffer[0] / 32768.0f;
    sample->im = (float)buffer[1] / 32768.0f;

    stream->samples_processed++;
    return 1;
}

void wav_write_sample(wav_stream_t *stream, const cplx_f32 *sample) {
    short buffer[2];
    float re = sample->re * 32768.0f;
    float im = sample->im * 32768.0f;

    /* Жесткое ограничение (Clipping) для защиты от переполнения */
    if (re >  32767.0f) re =  32767.0f;
    if (re < -32768.0f) re = -32768.0f;
    if (im >  32767.0f) im =  32767.0f;
    if (im < -32768.0f) im = -32768.0f;

    buffer[0] = (short)re; /* Left  = I */
    buffer[1] = (short)im; /* Right = Q */

    fwrite(buffer, sizeof(short), 2, stream->file);
    stream->samples_processed++;
}

void wav_close(wav_stream_t *stream) {
    if (!stream->file) return;

    if (stream->is_writing) {
        /* Фиксируем реальные размеры данных */
        unsigned int data_size = stream->samples_processed * stream->header.block_align;
        stream->header.subchunk2_size = data_size;
        stream->header.chunk_size = sizeof(wav_header_t) - 8 + data_size;

        /* Возвращаемся в начало и перезаписываем заголовок */
        fseek(stream->file, 0, SEEK_SET);
        fwrite(&stream->header, sizeof(wav_header_t), 1, stream->file);
    }

    fclose(stream->file);
    stream->file = NULL;
}
