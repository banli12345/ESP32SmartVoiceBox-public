#include "serial_upload_protocol.h"
#include <stdio.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(c) do { ++checks; if(!(c)) { ++failures; fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); } } while(0)
int main(void) {
    CHECK(sdup_crc32(0,"123456789",9)==0xcbf43926u);
    CHECK(sdup_crc32(sdup_crc32(0,"1234",4),"56789",5)==0xcbf43926u);
    CHECK(sdup_crc32(0,NULL,0)==0);
    uint8_t h[12]={'S','D','U','1',SDUP_DATA,0}; uint8_t op=0;
    uint16_t len=0; uint32_t seq=0;
    sdup_put_u16(h+6,SDUP_MAX_PAYLOAD); sdup_put_u32(h+8,0x89abcdefu);
    CHECK(sdup_decode_header(h,&op,&len,&seq));
    CHECK(op==SDUP_DATA && len==SDUP_MAX_PAYLOAD && seq==0x89abcdefu);
    h[5]=1; CHECK(!sdup_decode_header(h,0,0,0)); h[5]=0;
    sdup_put_u16(h+6,SDUP_MAX_PAYLOAD+1); CHECK(!sdup_decode_header(h,0,0,0));
    CHECK(sdup_valid_path("/wallpapers/wallpaper1.bin"));
    CHECK(sdup_valid_path("/wallpapers/wallpaper3.bin"));
    CHECK(!sdup_valid_path("/wallpapers/other.bin"));
    CHECK(sdup_valid_path("/music/晴天.wav"));
    CHECK(sdup_valid_path("/music/晴天.lrc"));
    CHECK(sdup_valid_path("/imges/照片.bin"));
    CHECK(sdup_valid_path("/videos/ikun20.rvf"));
    CHECK(sdup_valid_path("/videos/ikun20.wav"));
    CHECK(!sdup_valid_path("/music/../fonts/foo.wav"));
    CHECK(!sdup_valid_path("/music/nested/song.wav"));
    CHECK(!sdup_valid_path("/fonts/chinese_20.bin"));
    CHECK(!sdup_valid_path("/music/song.mp3"));
    CHECK(!sdup_valid_path("/music/song.wav "));
    CHECK(!sdup_valid_path("/music/.secret.wav"));
    CHECK(!sdup_valid_path("/music/a\\b.wav"));
    CHECK(!sdup_valid_path("/videos/\xc0\xaf.rvf"));
    CHECK(!sdup_valid_path("/videos/\xed\xa0\x80.rvf"));
    CHECK(!sdup_valid_path("/videos/\xf4\x90\x80\x80.rvf"));
    char longpath[96]="/music/";
    memset(longpath+7,'a',53); strcpy(longpath+60,".wav");
    CHECK(!sdup_valid_path(longpath));
    printf("serial upload protocol: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
