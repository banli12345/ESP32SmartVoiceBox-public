"""Check native HTTP completion semantics, including a truncated chunked body."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc')
    args = parser.parse_args()
    bundled = ROOT / '.tools/host-tcc/tcc/tcc.exe'
    cc = args.cc or (str(bundled) if bundled.is_file() else shutil.which('cc'))
    if not cc:
        parser.error('Pass --cc with a C compiler')
    text = (ROOT / 'src/WebBrowser.cpp').read_text(encoding='utf-8')
    match = re.search(r'bool body_is_complete\([^;{}]*\)\s*\{', text)
    assert match, 'Native HTTP completion helper missing'
    at, depth = match.end(), 1
    while depth:
        depth += (text[at] == '{') - (text[at] == '}')
        at += 1
    production = text[match.start():at]
    fake = r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef struct { int64_t length; bool chunked, complete; } Http;
typedef Http *esp_http_client_handle_t;
static int64_t esp_http_client_get_content_length(Http *h) { return h->length; }
static bool esp_http_client_is_chunked_response(Http *h) { return h->chunked; }
static bool esp_http_client_is_complete_data_received(Http *h) { return h->complete; }
'''
    cases = r'''
int main(void) {
    Http h = {0, false, false}; assert(body_is_complete(&h));
    h.chunked=true; assert(!body_is_complete(&h));
    h.complete=true; assert(body_is_complete(&h));
    h.length=512; h.chunked=false; h.complete=false; assert(!body_is_complete(&h));
    h.complete=true; assert(body_is_complete(&h));
    h.length=-1; h.complete=false; assert(!body_is_complete(&h));
    h.complete=true; assert(body_is_complete(&h));
    puts("Native HTTP completion passed: empty, content-length, unknown length, complete/truncated chunked");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='native-http-', ignore_cleanup_errors=True) as temp:
        unit, exe = Path(temp)/'http.c', Path(temp)/'http.exe'
        unit.write_text(fake + production + cases, encoding='utf-8')
        subprocess.run([cc, str(unit), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
