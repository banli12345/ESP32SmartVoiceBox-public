#include "serial_upload_protocol.h"
#include <string.h>
uint16_t sdup_u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
uint32_t sdup_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
void sdup_put_u16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
void sdup_put_u32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
uint32_t sdup_crc32(uint32_t crc, const void *data, size_t length) {
    const uint8_t *p=(const uint8_t *)data;
    crc=~crc;
    while (length--) {
        crc ^= *p++;
        for (unsigned i=0; i<8; ++i)
            crc=(crc>>1) ^ (0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
int sdup_decode_header(const uint8_t h[SDUP_HEADER_SIZE], uint8_t *op,
                       uint16_t *len, uint32_t *seq) {
    if (!h || memcmp(h,"SDU1",4) || h[5] || sdup_u16(h+6)>SDUP_MAX_PAYLOAD) return 0;
    if (op) *op=h[4];
    if (len) *len=sdup_u16(h+6);
    if (seq) *seq=sdup_u32(h+8);
    return 1;
}
static int valid_utf8(const unsigned char *p) {
    while (*p) {
        uint32_t cp;
        unsigned n;
        if (*p<0x80) { ++p; continue; }
        if (*p>=0xc2 && *p<=0xdf) { cp=*p&31u; n=1; }
        else if (*p>=0xe0 && *p<=0xef) { cp=*p&15u; n=2; }
        else if (*p>=0xf0 && *p<=0xf4) { cp=*p&7u; n=3; }
        else return 0;
        ++p;
        for (unsigned i=0; i<n; ++i) {
            if ((*p&0xc0)!=0x80) return 0;
            cp=(cp<<6)|(*p++&63u);
        }
        if ((n==2 && cp<0x800) || (n==3 && cp<0x10000) ||
            (cp>=0xd800 && cp<=0xdfff) || cp>0x10ffff) return 0;
    }
    return 1;
}
int sdup_valid_path(const char *path) {
    const char *name, *ext;
    size_t len;
    if (!path || !(len=strlen(path)) || len>SDUP_PATH_MAX || strstr(path,"..")) return 0;
    name=strrchr(path,'/');
    if (!name || !name[1] || name[1]=='.' || path[len-1]=='.' || path[len-1]==' ') return 0;
    if (name != strchr(path+1,'/')) return 0;
    for (const unsigned char *p=(const unsigned char *)name+1; *p; ++p)
        if (*p<32 || *p==127 || strchr("<>:\"\\|?*",*p)) return 0;
    if (!valid_utf8((const unsigned char *)path)) return 0;
    ext=strrchr(name+1,'.');
    if (!ext) return 0;
    if (!strncmp(path,"/wallpapers/",12))
        return !strcmp(name+1,"wallpaper1.bin") || !strcmp(name+1,"wallpaper2.bin") ||
               !strcmp(name+1,"wallpaper3.bin");
    if (!strncmp(path,"/music/",7)) return !strcmp(ext,".wav") || !strcmp(ext,".lrc");
    if (!strncmp(path,"/imges/",7)) return !strcmp(ext,".bin");
    if (!strncmp(path,"/videos/",8)) return !strcmp(ext,".rvf") || !strcmp(ext,".wav");
    return 0;
}
