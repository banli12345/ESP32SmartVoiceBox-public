"""Execute the production SD-icon loader with no internal-memory image budget."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx')
    args=parser.parse_args()
    compiler=args.cxx or shutil.which('cl') or shutil.which('c++') or shutil.which('g++')
    if not compiler:
        parser.error('Run in a C++ developer terminal or pass --cxx')
    source=(ROOT/'src/SDcar.cpp').read_text(encoding='utf-8')
    match=re.search(r'static bool load_sd_icon\([^;{}]*\)\s*\{',source)
    assert match, 'Production icon loader missing'
    end,depth=match.end(),1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    body=source[match.start():end]
    sdk=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
static constexpr unsigned MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2;
static constexpr unsigned LV_IMG_CF_TRUE_COLOR_ALPHA=4;
static constexpr unsigned LV_IMG_PX_SIZE_ALPHA_BYTE=3;
static constexpr size_t SD_FILE_BUFFER_SIZE=32768;
struct lv_img_header_t { uint32_t cf,w,h; };
struct lv_img_dsc_t { lv_img_header_t header{}; size_t data_size{}; const uint8_t *data{}; };
static unsigned external_allocations,external_frees;
static size_t external_bytes;
static bool fail_external,short_payload;
static void *heap_caps_malloc(size_t n,unsigned caps) {
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(fail_external) return nullptr;
    ++external_allocations; external_bytes+=n; return std::malloc(n);
}
static void heap_caps_free(void *p) { if(p) ++external_frees; std::free(p); }
struct StorageStream {
    bool header_read=false;
    bool active() const {return true;}
    void use_buffer(size_t) {}
    void close() {}
    size_t read(void *out,size_t n) {
        if(!header_read){const lv_img_header_t h{LV_IMG_CF_TRUE_COLOR_ALPHA,64,64};
            assert(n==sizeof(h));std::memcpy(out,&h,n);header_read=true;return n;}
        std::memset(out,0x5a,n);return short_payload?n-1:n;
    }
};
static StorageStream sd_open_read(const char *) {return {};}
'''
    tests=r'''
int main() {
    lv_img_dsc_t images[9]{};
    for(auto &image:images) {assert(load_sd_icon("fixture",&image));assert(image.data_size==12288);}
    assert(external_allocations==9 && external_bytes==110592);
    for(auto &image:images) heap_caps_free((void *)image.data);
    lv_img_dsc_t failed{};fail_external=true;
    assert(!load_sd_icon("fixture",&failed) && failed.data==nullptr);
    fail_external=false;short_payload=true;
    assert(!load_sd_icon("fixture",&failed) && failed.data==nullptr);
    assert(external_allocations==10 && external_frees==10);
    puts("SD icon memory passed: 108 KiB goes to PSRAM, exhausted PSRAM fails safely, short reads release the buffer");
}
'''
    with tempfile.TemporaryDirectory(prefix='icon-memory-',ignore_cleanup_errors=True) as temporary:
        unit=Path(temporary)/'icons.cpp';exe=Path(temporary)/'icons.exe'
        unit.write_text(sdk+body+tests,encoding='utf-8')
        if Path(compiler).stem.lower()=='cl':
            command=[compiler,'/nologo','/std:c++17','/EHsc','/utf-8',str(unit),'/Fe:'+str(exe),'/Fo:'+str(Path(temporary)/'icons.obj')]
        else:
            command=[compiler,'-std=c++17',str(unit),'-o',str(exe)]
        subprocess.run(command,check=True)
        subprocess.run([str(exe)],check=True)


if __name__=='__main__':
    main()
