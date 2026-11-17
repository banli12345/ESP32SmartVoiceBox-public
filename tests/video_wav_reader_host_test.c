/* Real filesystem + real WAV parser: stream the deployed companion repeatedly
 * while another descriptor reads the RVF. No fake file I/O in this test. */
#include "video_wav_reader.h"
#include "wav_pcm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    video_wav_reader_t wav = VIDEO_WAV_READER_INIT;
    video_wav_reader_t rvf = VIDEO_WAV_READER_INIT;
    wav_pcm_reader_t parser;
    wav_pcm_info_t info;
    unsigned char actual[2048], expected[2048], picture[4096];
    FILE *gold;
    unsigned session, cycle;
    assert(argc == 3);
    gold = fopen(argv[1], "rb");
    assert(gold);
    assert(video_wav_reader_open(&rvf, argv[2]) == 0);

    for (session = 0; session < 3; ++session) {
        assert(video_wav_reader_open(&wav, argv[1]) == 0);
        parser.read = video_wav_reader_read;
        parser.seek = video_wav_reader_seek;
        parser.ctx = &wav;
        parser.size = wav.size;
        parser.pos = 0;
        assert(wav_pcm_parse(&parser, &info) == WAV_PCM_OK);
        assert(info.sample_rate == 44100 && info.sample_frames == 1461915);
        for (cycle = 0; cycle < 2; ++cycle) {
            uint32_t left = info.data_bytes;
            assert(video_wav_reader_seek(&wav, info.data_offset) == 0);
            assert(fseek(gold, info.data_offset, SEEK_SET) == 0);
            while (left) {
                uint32_t count = left < sizeof(actual) ? left : sizeof(actual);
                assert(video_wav_reader_read(&wav, actual, count) == (int)count);
                assert(fread(expected, 1, count, gold) == count);
                assert(memcmp(actual, expected, count) == 0);
                assert(video_wav_reader_read(&rvf, picture, sizeof(picture)) == sizeof(picture));
                left -= count;
            }
            assert(wav.position == info.data_offset + info.data_bytes);
        }
        /* Invalid seek must leave the current cursor intact. */
        assert(video_wav_reader_seek(&wav, wav.size + 1) == -1);
        assert(video_wav_reader_seek(&wav, wav.size - 7) == 0);
        assert(video_wav_reader_read(&wav, actual, 2048) == 7);
        assert(video_wav_reader_read(&wav, actual, 2048) == 0);
        video_wav_reader_close(&wav);
        video_wav_reader_close(&wav);
        assert(wav.fd == -1 && wav.position == 0 && wav.size == 0);
        assert(video_wav_reader_read(&wav, actual, 12) == -1);
    }
    assert(video_wav_reader_open(&wav, "no-such-video-companion.wav") == -1);
    assert(wav.fd == -1);
    video_wav_reader_close(&rvf);
    fclose(gold);
    puts("video WAV reader passed: actual WAV parse, 3 reopens, 6 byte-exact PCM loops with interleaved RVF reads, EOF and invalid seek");
    return 0;
}
