"""Host test for the TTS PCM ring. No device, network, or credentials."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>
bool fail_alloc=false;
#include "@ROOT@/src/AiSpeechBuffer.cpp"

static void sequential() {
    AiSpeechRing *ring = ai_speech_ring_create(8);
    assert(ring && ai_speech_ring_capacity(ring)==8);
    uint8_t out[8] = {};
    assert(ai_speech_ring_write(ring, (const uint8_t*)"abcdef", 6)==6);
    assert(ai_speech_ring_fill(ring)==6);
    assert(ai_speech_ring_write(ring, (const uint8_t*)"zzzzzzzz", 8)==2);
    assert(ai_speech_ring_peak(ring)==8);
    assert(ai_speech_ring_write(ring, (const uint8_t*)"!", 1)==0);
    assert(ai_speech_ring_read(ring, out, 3)==3);
    assert(!memcmp(out, "abc", 3));
    assert(ai_speech_ring_write(ring, (const uint8_t*)"XYZ", 3)==3);
    assert(ai_speech_ring_read(ring, out, 8)==8);
    assert(!memcmp(out, "defzzXYZ", 8));
    assert(ai_speech_ring_fill(ring)==0);
    uint8_t odd[3] = {1, 2, 3};
    assert(ai_speech_ring_write(ring, odd, 3)==3);
    assert(ai_speech_ring_read(ring, out, 1)==1 && out[0]==1);
    assert(ai_speech_ring_read(ring, out, 2)==2 && out[0]==2 && out[1]==3);
    ai_speech_ring_destroy(ring);
    ai_speech_ring_destroy(nullptr);
    fail_alloc=true;
    assert(!ai_speech_ring_create(64));
    fail_alloc=false;
}

static void concurrent() {
    constexpr size_t cap = 64 * 1024;
    constexpr size_t total = cap * 3 + 17;
    AiSpeechRing *ring = ai_speech_ring_create(cap);
    assert(ring);
    std::vector<uint8_t> src(total), got(total);
    for (size_t i = 0; i < total; ++i) src[i] = (uint8_t)(i * 17 + 3);
    std::atomic<size_t> produced{0}, consumed{0};
    std::thread producer([&] {
        size_t off = 0;
        while (off < total) {
            size_t n = 1000 + (off % 3000);
            if (n > total - off) n = total - off;
            size_t w = ai_speech_ring_write(ring, src.data() + off, n);
            off += w;
            produced.store(off);
            if (!w) std::this_thread::yield();
        }
    });
    std::thread consumer([&] {
        size_t off = 0;
        while (off < total) {
            size_t n = 1 + (off % 2500);
            size_t r = ai_speech_ring_read(ring, got.data() + off, n);
            off += r;
            consumed.store(off);
            if (!r) std::this_thread::yield();
        }
    });
    producer.join();
    consumer.join();
    assert(produced.load()==total && consumed.load()==total);
    assert(got==src);
    assert(ai_speech_ring_fill(ring)==0);
    assert(ai_speech_ring_peak(ring)>0 && ai_speech_ring_peak(ring)<=cap);
    ai_speech_ring_destroy(ring);
}

int main() {
    sequential();
    concurrent();
    puts("ai speech ring: wrap, full queue, odd bytes, OOM, concurrent order passed");
}
'''


def main():
    compiler = shutil.which('cl')
    if not compiler:
        raise SystemExit('Use a Visual Studio C++ developer terminal')
    with tempfile.TemporaryDirectory(prefix='ai-ring-', ignore_cleanup_errors=True) as tmp:
        directory = Path(tmp)
        (directory / 'esp_heap_caps.h').write_text(
            '#pragma once\n#include <cstdlib>\n'
            'enum {MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_INTERNAL=4};\n'
            'extern bool fail_alloc;\n'
            'inline void *heap_caps_malloc(size_t n, unsigned){return fail_alloc?nullptr:std::malloc(n);}\n'
            'inline void heap_caps_free(void *p){std::free(p);}\n', encoding='utf-8')
        unit = directory / 'test.cpp'
        unit.write_text(HARNESS.replace('@ROOT@', ROOT.as_posix()), encoding='utf-8')
        exe = directory / 'test.exe'
        subprocess.run([compiler, '/nologo', '/std:c++17', '/EHsc', '/utf-8',
                        '/D_CRT_SECURE_NO_WARNINGS', f'/I{directory}', f'/I{ROOT / "src"}',
                        str(unit), f'/Fe:{exe}', f'/Fo{directory}\\'], check=True)
        subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == '__main__':
    main()
