#ifndef SERIAL_UPLOAD_PROTOCOL_H
#define SERIAL_UPLOAD_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SDUP_CHUNK_SIZE 16384u
#define SDUP_MAX_PAYLOAD (SDUP_CHUNK_SIZE + 6u)
#define SDUP_PATH_MAX 63u
#define SDUP_MAX_FILES 32u
#define SDUP_HEADER_SIZE 12u
enum { SDUP_PING=1, SDUP_BEGIN=2, SDUP_DATA=3, SDUP_FINISH=4,
       SDUP_COMMIT=5, SDUP_EXIT=6, SDUP_ABORT=7,
       SDUP_ACK=128, SDUP_ERROR=129, SDUP_PROGRESS=130 };
uint16_t sdup_u16(const uint8_t *p);
uint32_t sdup_u32(const uint8_t *p);
void sdup_put_u16(uint8_t *p, uint16_t value);
void sdup_put_u32(uint8_t *p, uint32_t value);
/* Incremental IEEE CRC32, initial/final values match Python zlib.crc32. */
uint32_t sdup_crc32(uint32_t crc, const void *data, size_t length);
int sdup_decode_header(const uint8_t header[SDUP_HEADER_SIZE], uint8_t *opcode,
                       uint16_t *length, uint32_t *sequence);
int sdup_valid_path(const char *path);
#ifdef __cplusplus
}
#endif
#endif
