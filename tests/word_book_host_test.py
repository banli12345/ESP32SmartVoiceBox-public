"""Run the production indexed reader against every supplied word and damaged SD files."""
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from convert_word_book import parse_text, encode_book
OUT = ROOT / '.tools/word-match-20261115/host-reader'
SOURCE = r'''
#include "WordBook.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
int main(int argc,char**argv){
 assert(argc==3);word_book_t b={0};word_book_entry_t e;int mode=atoi(argv[2]);
 if(mode==1){assert(!word_book_open(&b,argv[1])&&!b.file&&!b.count);return 0;}
 assert(word_book_open(&b,argv[1])&&b.count);
 if(mode==2){memset(&e,0xff,sizeof(e));assert(!word_book_read(&b,0,&e)&&!e.english[0]&&!e.chinese[0]);}
 else {for(uint32_t i=0;i<b.count;i++){assert(word_book_read(&b,i,&e));printf("%s\t%s\n",e.english,e.chinese);}assert(!word_book_read(&b,b.count,&e));}
 word_book_close(&b);word_book_close(&b);assert(!b.file&&!b.count);assert(!word_book_read(&b,0,&e));
 assert(!word_book_open(&b,"this-file-does-not-exist.bwv"));return 0;
}
'''


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    unit = OUT / 'test.c'; unit.write_text(SOURCE, encoding='utf-8')
    exe = OUT / 'reader.exe'
    subprocess.run([str(ROOT/'.tools/host-tcc/tcc/tcc.exe'), '-I'+str(ROOT/'src'), str(unit),
                    str(ROOT/'src/WordBook.c'), '-o', str(exe)], check=True)
    records = parse_text((ROOT/'assets/words/cet4.txt').read_text(encoding='utf-8-sig'))
    raw = (ROOT/'sd_card_files/words/cet4.bwv').read_bytes()
    assert raw == encode_book(records)
    result = subprocess.run([str(exe), str(ROOT/'sd_card_files/words/cet4.bwv'), '0'], check=True, capture_output=True)
    assert result.stdout.decode('utf-8').splitlines() == [a+'\t'+b for a,b in records]
    faults = []
    def change(name, offset, value, mode=1):
        bad = bytearray(raw); bad[offset:offset+len(value)] = value; faults.append((name, bad, mode))
    change('magic',0,b'BAD!'); change('zero-count',4,struct.pack('<I',0))
    change('huge-count',4,struct.pack('<I',100001)); change('index',8,struct.pack('<I',0xffffffff))
    change('data',12,struct.pack('<I',0xffffffff)); change('reserved',20,struct.pack('<I',1))
    change('first',24,struct.pack('<I',1))
    change('last',24+len(records)*4,struct.pack('<I',len(raw)))
    change('empty-record',28,struct.pack('<I',0),2)
    payload = struct.unpack_from('<I',raw,12)[0]
    first_end = struct.unpack_from('<I',raw,28)[0]
    change('missing-terminator',payload+first_end-1,b'A',2)
    change('empty-english',payload,b'\0',2)
    change('overlong-utf8',payload,b'\xc0\xaf',2)
    meaning = payload + len(records[0][0].encode()) + 1
    change('surrogate',meaning,b'\xed\xa0\x80',2)
    change('out-of-range-unicode',meaning,b'\xf4\x90\x80\x80',2)
    change('meaning-inner-nul',meaning+1,b'\0',2)
    faults.extend([('truncated',raw[:-1],1),('trailer',raw+b'x',1),('tiny',raw[:12],1),
                   ('oversized-word',encode_book([('a'*48,'中文')]),2),
                   ('oversized-meaning',encode_book([('word','中'*43)]),2)])
    for name,data,mode in faults:
        path=OUT/(name+'.bwv');path.write_bytes(data)
        subprocess.run([str(exe),str(path),str(mode)],check=True,capture_output=True)
    for bad in ('word meaning','word\t', 'word  \x00bad', 'a'*48+'  中文'):
        try: parse_text(bad)
        except ValueError: pass
        else: raise AssertionError('Invalid source accepted')
    assert parse_text('\ufeffhello\t你好\r\n\r\nworld  世界') == [('hello','你好'),('world','世界')]
    print(f'Word book PASS: {len(records)} exact source pairs, {len(faults)} malformed files, UTF-8, capacities and parser validation')


if __name__ == '__main__':
    main()
