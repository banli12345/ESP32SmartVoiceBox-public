#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct firmware_image_info_t {char version[32], project[32], time[16], date[16];uint8_t elf_sha[32];uint32_t size;};
bool firmware_image_validate(const char *path, const char *project, uint32_t limit,
    firmware_image_info_t *info, char *error, size_t error_size);
