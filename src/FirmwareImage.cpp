#include "FirmwareImage.h"
#include "Storage.h"
#include "mbedtls/sha256.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
bool firmware_image_validate(const char *path,const char *project,uint32_t limit,
 firmware_image_info_t *info,char *error,size_t capacity)
{
 if(error&&capacity)*error=0;
 auto fail=[&](const char *text){if(error&&capacity)snprintf(error,capacity,"%s",text);return false;};
 StorageStream file;
 if(!info||!file.open(path,"rb"))return fail("没有已上传的固件，请先通过热点网页上传");
 const uint64_t size=file.byte_count();
 if(size<320||size>limit)return fail("固件大小无效或超出 OTA 分区");
 uint8_t h[24],desc[256],buffer[1024];
 if(file.read(h,24)!=24||h[0]!=0xe9||!h[1]||h[1]>16||h[12]!=9||h[13]||h[23]!=1)
  return fail("请选择 ESP32-S3 应用固件，不能使用整片 Flash 镜像");
 mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);
 struct Cleanup{mbedtls_sha256_context *p;~Cleanup(){mbedtls_sha256_free(p);}} cleanup{&hash};
 if(mbedtls_sha256_starts(&hash,0)||mbedtls_sha256_update(&hash,h,24))return fail("固件校验初始化失败");
 uint64_t at=24;uint8_t checksum=0xef;
 for(unsigned segment=0;segment<h[1];++segment){
  uint8_t header[8];if(at+8>size||file.read(header,8)!=8)return fail("固件段信息不完整");
  at+=8;const uint32_t length=u32(header+4);
  if(length>size-at||(!segment&&length<sizeof(desc)))return fail("固件段长度无效");
  if(mbedtls_sha256_update(&hash,header,8))return fail("固件校验失败");
  uint32_t left=length;bool first=true;
  while(left){
   const size_t n=std::min<size_t>(left,sizeof(buffer));
   if(file.read(buffer,n)!=n)return fail("固件读取失败");
   if(!segment&&first){memcpy(desc,buffer,sizeof(desc));}
   first=false;
   for(size_t i=0;i<n;++i)checksum^=buffer[i];
   if(mbedtls_sha256_update(&hash,buffer,n))return fail("固件校验失败");
   left-=n;at+=n;
  }
 }
 const size_t padding=16-(at%16);
 if(at+padding+32!=size||file.read(buffer,padding)!=padding||buffer[padding-1]!=checksum)
  return fail("固件长度或校验和不匹配，文件可能未上传完整");
 uint8_t calculated[32],expected[32];
 if(mbedtls_sha256_update(&hash,buffer,padding)||mbedtls_sha256_finish(&hash,calculated)||
    file.read(expected,32)!=32||memcmp(calculated,expected,32))return fail("固件 SHA-256 校验失败，请重新上传");
 if(u32(desc)!=0xabcd5432||!memchr(desc+16,0,32)||!memchr(desc+48,0,32)||strcmp((const char*)desc+48,project))
  return fail("固件工程不匹配，请上传本设备工程的应用 .bin");
 memset(info,0,sizeof(*info));
 memcpy(info->version,desc+16,31);memcpy(info->project,desc+48,31);
 memcpy(info->time,desc+80,15);memcpy(info->date,desc+96,15);memcpy(info->elf_sha,desc+144,32);
 info->size=(uint32_t)size;return true;
}
