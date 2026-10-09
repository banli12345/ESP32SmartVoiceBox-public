#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <strings.h>
#include "SDcar.h"
#include "SerialUpload.h"
#include "serial_upload_protocol.h"

namespace {
constexpr uint32_t LOG_BAUD=115200, UPLOAD_BAUD=921600;
constexpr unsigned MAX_ENTRIES=SDUP_MAX_FILES*2;
constexpr const char *JOURNAL="/.sdup-journal", *JOURNAL_NEW="/.sdup-journal-new";
RTC_DATA_ATTR uint32_t upload_latch=0;
constexpr uint32_t LATCH=0x53445531;
struct Entry {
    char path[SDUP_PATH_MAX+1];
    uint32_t size, crc, received, running_crc;
    bool verified, remove_only, had_original;
};
Entry *entries=nullptr;
unsigned entry_count=0, file_count=0;
int open_index=-1;
File current;
uint8_t *buffer=nullptr;
bool staged=false;

void auxiliary_path(const char *path, const char *suffix, char out[96]) {
    snprintf(out,96,"%s%s",path,suffix);
}
bool is_file(const char *path) {
    File f=SD_MMC.open(path,FILE_READ);
    bool result=f && !f.isDirectory();
    if (f) f.close();
    return result;
}
bool remove_if_exists(const char *path) {
    return !SD_MMC.exists(path) || SD_MMC.remove(path);
}
void close_current() {
    if (current) { current.flush(); current.close(); }
    open_index=-1;
}
void reset_entries() {
    close_current();
    memset(entries,0,MAX_ENTRIES*sizeof(Entry));
    file_count=entry_count=0;
    staged=false;
}
bool discard_staging() {
    close_current();
    bool ok=true;
    for (unsigned i=0; i<entry_count; ++i) {
        char path[96]; auxiliary_path(entries[i].path,".sdup-part",path);
        if (!remove_if_exists(path)) ok=false;
    }
    return ok;
}
bool recover_journal() {
    // A journal is installed before any original is renamed. DONE is appended
    // after every replacement, making recovery idempotent across resets.
    File f=SD_MMC.open(JOURNAL,FILE_READ);
    if (!f) return remove_if_exists(JOURNAL_NEW);
    size_t size=f.size();
    if (size<12 || size>MAX_ENTRIES*(SDUP_PATH_MAX+2)+16) { f.close(); return false; }
    if (f.read(buffer,size)!=size) { f.close(); return false; }
    f.close();
    if (memcmp(buffer,"SDJ1",4) || sdup_u16(buffer+6)) return false;
    unsigned count=sdup_u16(buffer+4);
    if (!count || count>MAX_ENTRIES) return false;
    size_t at=8;
    for (unsigned i=0; i<count; ++i) {
        if (at+2>size) return false;
        unsigned flags=buffer[at++], len=buffer[at++];
        if (flags>3 || !len || len>SDUP_PATH_MAX || at+len+4>size) return false;
        Entry &e=entries[i];
        memset(&e,0,sizeof(e));
        memcpy(e.path,buffer+at,len); at+=len;
        if (!sdup_valid_path(e.path)) return false;
        for (unsigned j=0;j<i;++j) if (!strcasecmp(entries[j].path,e.path)) return false;
        e.had_original=flags&1; e.remove_only=flags&2;
    }
    if (size<at+4 || size>at+8) return false;
    if (sdup_u32(buffer+at)!=sdup_crc32(0,buffer,at)) return false;
    bool committed=at+8==size && !memcmp(buffer+at+4,"DONE",4);
    // A partially written DONE marker means rollback, never accept a partial
    // batch. A complete but invalid marker is a corrupt journal.
    if (at+8==size && !committed) return false;
    for (unsigned i=0; i<count; ++i) {
        Entry &e=entries[i];
        char backup[96], part[96];
        auxiliary_path(e.path,".sdup-bak",backup);
        auxiliary_path(e.path,".sdup-part",part);
        if (committed) {
            if (!remove_if_exists(backup)) return false;
        } else if (e.had_original && SD_MMC.exists(backup)) {
            if (!remove_if_exists(e.path) || !SD_MMC.rename(backup,e.path)) return false;
        } else if (!e.had_original) {
            if (!remove_if_exists(e.path)) return false;
        } else if (!is_file(e.path)) {
            return false;
        }
        if (!remove_if_exists(part)) return false;
    }
    return remove_if_exists(JOURNAL) && remove_if_exists(JOURNAL_NEW);
}
void response(uint8_t op, uint32_t seq, const void *payload=nullptr, uint16_t length=0) {
    uint8_t h[SDUP_HEADER_SIZE]={ 'S','D','U','1',op,0,0,0,0,0,0,0 }, trailer[4];
    sdup_put_u16(h+6,length); sdup_put_u32(h+8,seq);
    uint32_t crc=sdup_crc32(0,h+4,8);
    crc=sdup_crc32(crc,payload,length); sdup_put_u32(trailer,crc);
    Serial.write(h,sizeof(h));
    if (length) Serial.write(static_cast<const uint8_t *>(payload),length);
    Serial.write(trailer,4);
}
void error(uint32_t seq, const char *message) {
    response(SDUP_ERROR,seq,message,strlen(message));
}
bool read_exact(uint8_t *dst, size_t length, uint32_t timeout_ms=5000) {
    uint32_t last=millis(); size_t got=0;
    while (got<length) {
        int available=Serial.available();
        if (available>0) {
            size_t n=length-got;
            if (n>(size_t)available) n=available;
            size_t read=Serial.readBytes(dst+got,n);
            got+=read; if (read) last=millis();
        } else delay(1);
        if (millis()-last>timeout_ms) return false;
    }
    return true;
}
bool install_journal() {
    size_t at=8;
    memcpy(buffer,"SDJ1",4);
    sdup_put_u16(buffer+4,entry_count); sdup_put_u16(buffer+6,0);
    for (unsigned i=0; i<entry_count; ++i) {
        Entry &e=entries[i];
        e.had_original=is_file(e.path);
        if (SD_MMC.exists(e.path) && !e.had_original) return false;
        unsigned len=strlen(e.path);
        buffer[at++]=(e.had_original?1:0)|(e.remove_only?2:0);
        buffer[at++]=len;
        memcpy(buffer+at,e.path,len); at+=len;
        char backup[96]; auxiliary_path(e.path,".sdup-bak",backup);
        // An unexplained backup must be preserved for manual recovery.
        if (SD_MMC.exists(backup)) return false;
    }
    sdup_put_u32(buffer+at,sdup_crc32(0,buffer,at)); at+=4;
    if (!remove_if_exists(JOURNAL_NEW)) return false;
    File f=SD_MMC.open(JOURNAL_NEW,FILE_WRITE);
    if (!f) return false;
    bool ok=f.write(buffer,at)==at; f.flush(); f.close();
    return ok && SD_MMC.rename(JOURNAL_NEW,JOURNAL);
}
const char *begin_batch(const uint8_t *p, uint16_t len) {
    if (staged) return "batch already active";
    if (len<2) return "invalid manifest";
    unsigned count=sdup_u16(p); size_t at=2;
    if (!count || count>SDUP_MAX_FILES) return "too many files";
    reset_entries();
    uint64_t total=0;
    for (unsigned i=0; i<count; ++i) {
        if (at+10>len) { reset_entries(); return "invalid manifest"; }
        unsigned path_len=sdup_u16(p+at); at+=2;
        Entry &e=entries[i];
        e.size=sdup_u32(p+at); e.crc=sdup_u32(p+at+4); at+=8;
        if (!path_len || path_len>SDUP_PATH_MAX || at+path_len>len || e.size>0x7fffffffu) {
            reset_entries(); return "invalid manifest";
        }
        memcpy(e.path,p+at,path_len); at+=path_len;
        if (memchr(e.path,0,path_len) || !sdup_valid_path(e.path)) {
            reset_entries(); return "invalid SD path";
        }
        for (unsigned j=0; j<i; ++j) if (!strcasecmp(e.path,entries[j].path)) {
            reset_entries(); return "duplicate target path";
        }
        total+=e.size;
    }
    if (at!=len) { reset_entries(); return "invalid manifest length"; }
    file_count=entry_count=count;
    // Replacing a silent RVF also removes an old, now unrelated soundtrack.
    for (unsigned i=0; i<file_count; ++i) {
        const char *ext=strrchr(entries[i].path,'.');
        if (!ext || strcmp(ext,".rvf")) continue;
        char wav[SDUP_PATH_MAX+1]; strcpy(wav,entries[i].path);
        strcpy(strrchr(wav,'.'),".wav");
        bool paired=false;
        for (unsigned j=0;j<file_count;++j) if (!strcasecmp(entries[j].path,wav)) paired=true;
        if (!paired && SD_MMC.exists(wav)) {
            Entry &e=entries[entry_count++]; strcpy(e.path,wav);
            e.remove_only=e.verified=true;
        }
    }
    if (total>SD_MMC.totalBytes()-SD_MMC.usedBytes()) {
        reset_entries(); return "not enough free SD space for staging";
    }
    for (unsigned i=0;i<entry_count;++i) {
        Entry &e=entries[i];
        char dir[SDUP_PATH_MAX+1]; strcpy(dir,e.path); *strrchr(dir,'/')=0;
        if (!SD_MMC.exists(dir) && !SD_MMC.mkdir(dir)) { reset_entries(); return "cannot create SD directory"; }
        char part[96],backup[96]; auxiliary_path(e.path,".sdup-part",part);
        auxiliary_path(e.path,".sdup-bak",backup);
        if (SD_MMC.exists(backup)) { reset_entries(); return "unresolved backup; recovery required"; }
        if (!remove_if_exists(part)) { reset_entries(); return "cannot clear old staging file"; }
    }
    staged=true;
    return nullptr;
}
const char *write_chunk(const uint8_t *p, uint16_t len) {
    if (!staged || len<6) return "no active file batch";
    unsigned index=sdup_u16(p); uint32_t offset=sdup_u32(p+2);
    if (index>=file_count) return "invalid file index";
    Entry &e=entries[index]; size_t bytes=len-6;
    if (e.verified || offset!=e.received || bytes>e.size-e.received || !bytes) return "invalid file offset";
    if (open_index!=(int)index) {
        close_current();
        char part[96]; auxiliary_path(e.path,".sdup-part",part);
        current=SD_MMC.open(part,e.received?FILE_APPEND:FILE_WRITE);
        if (!current) return "cannot open staging file";
        current.setBufferSize(4096); open_index=index;
    }
    if (current.write(p+6,bytes)!=bytes) return "SD write failed";
    e.running_crc=sdup_crc32(e.running_crc,p+6,bytes); e.received+=bytes;
    return nullptr;
}
const char *finish_file(const uint8_t *p, uint16_t len, uint32_t seq) {
    if (!staged || len!=2) return "invalid finish request";
    unsigned index=sdup_u16(p);
    if (index>=file_count) return "invalid file index";
    Entry &e=entries[index];
    if (e.verified) return nullptr;
    if (e.received!=e.size || e.running_crc!=e.crc) return "file size or checksum mismatch";
    close_current();
    char part[96]; auxiliary_path(e.path,".sdup-part",part);
    if (!e.size) { File empty=SD_MMC.open(part,FILE_WRITE); if (!empty) return "cannot create empty file"; empty.close(); }
    File f=SD_MMC.open(part,FILE_READ);
    if (!f || f.size()!=e.size) { if(f) f.close(); return "SD file size mismatch"; }
    uint32_t crc=0,done=0,last_progress=millis();
    while (done<e.size) {
        size_t n=e.size-done; if (n>SDUP_CHUNK_SIZE) n=SDUP_CHUNK_SIZE;
        size_t read=f.read(buffer,n);
        if (read!=n) { f.close(); return "SD verification read failed"; }
        crc=sdup_crc32(crc,buffer,n); done+=n;
        if (millis()-last_progress>500) {
            uint8_t progress[4]; sdup_put_u32(progress,done);
            response(SDUP_PROGRESS,seq,progress,4); last_progress=millis();
        }
        delay(1);
    }
    f.close();
    if (crc!=e.crc) return "SD verification checksum mismatch";
    e.verified=true;
    return nullptr;
}
const char *commit_batch() {
    if (!staged) return "no active batch";
    for (unsigned i=0; i<entry_count; ++i) if (!entries[i].verified) return "batch has unverified files";
    close_current();
    if (!install_journal()) return "cannot write commit journal";
    bool ok=true;
    for (unsigned i=0; i<entry_count && ok; ++i) {
        Entry &e=entries[i]; char part[96],backup[96];
        auxiliary_path(e.path,".sdup-part",part); auxiliary_path(e.path,".sdup-bak",backup);
        if (e.had_original) ok=SD_MMC.rename(e.path,backup);
        if (ok && !e.remove_only) ok=SD_MMC.rename(part,e.path);
    }
    if (ok) {
        File journal=SD_MMC.open(JOURNAL,FILE_APPEND);
        ok=journal && journal.write((const uint8_t *)"DONE",4)==4;
        if (journal) { journal.flush(); journal.close(); }
    }
    if (!recover_journal()) return "commit recovery failed; original backups retained";
    staged=false;
    if (!ok) return "commit failed; original files restored";
    reset_entries();
    return nullptr;
}
bool entry_command() {
    static char line[48]; static unsigned used=0;
    while (Serial.available()) {
        int ch=Serial.read();
        if (ch=='\n') {
            line[used]=0; used=0;
            if (!strcmp(line,"@SDUP1 HELLO")) return true;
        } else if (ch!='\r') {
            if (used+1<sizeof(line)) line[used++]=(char)ch;
            else used=0;
        }
    }
    return false;
}
void reboot() {
    Serial.flush(); delay(200); Serial.updateBaudRate(LOG_BAUD); upload_latch=0; ESP.restart();
}
void receive_session() {
    Serial.println("@SDUP1 READY 1 921600"); Serial.flush(); delay(80);
    Serial.updateBaudRate(UPLOAD_BAUD);
    uint32_t last_activity=millis(),last_sequence=0,last_crc=0;
    uint8_t last_opcode=0; bool have_last=false;
    char last_error[160]={0};
    unsigned matched=0;
    while (millis()-last_activity<30000) {
        if (!Serial.available()) { delay(1); continue; }
        uint8_t ch=Serial.read();
        const char *magic="SDU1";
        if (ch!=(uint8_t)magic[matched]) { matched=ch=='S'?1:0; continue; }
        if (++matched<4) continue;
        matched=0;
        uint8_t h[SDUP_HEADER_SIZE]={'S','D','U','1'}, trailer[4],op=0;
        uint16_t len=0; uint32_t seq=0;
        if (!read_exact(h+4,8) || !sdup_decode_header(h,&op,&len,&seq)) continue;
        if (!read_exact(buffer,len) || !read_exact(trailer,4)) continue;
        uint32_t crc=sdup_crc32(sdup_crc32(0,h+4,8),buffer,len);
        if (crc!=sdup_u32(trailer)) { error(seq,"retry: frame checksum mismatch"); continue; }
        last_activity=millis();
        if (op==SDUP_ABORT || op==SDUP_EXIT) {
            bool ok=!SD_MMC.exists(JOURNAL) || recover_journal();
            if (ok && staged) ok=discard_staging();
            if (ok) response(SDUP_ACK,seq); else error(seq,"staging cleanup failed; backups retained");
            reboot();
        }
        if (have_last && seq==last_sequence) {
            if (op!=last_opcode || crc!=last_crc) error(seq,"sequence reused with different request");
            else if (*last_error) error(seq,last_error);
            else response(SDUP_ACK,seq);
            continue;
        }
        if (have_last && seq!=last_sequence+1) { error(seq,"unexpected request sequence"); continue; }
        const char *failure=nullptr;
        switch (op) {
        case SDUP_PING: if(len) failure="invalid ping"; break;
        case SDUP_BEGIN: failure=begin_batch(buffer,len); break;
        case SDUP_DATA: failure=write_chunk(buffer,len); break;
        case SDUP_FINISH: failure=finish_file(buffer,len,seq); break;
        case SDUP_COMMIT: failure=len?"invalid commit":commit_batch(); break;
        default: failure="unknown command"; break;
        }
        have_last=true; last_sequence=seq; last_crc=crc; last_opcode=op;
        strlcpy(last_error,failure?failure:"",sizeof(last_error));
        if (failure) error(seq,failure); else response(SDUP_ACK,seq);
        last_activity=millis();
    }
    if (SD_MMC.exists(JOURNAL)) recover_journal();
    if (staged) discard_staging();
    reboot();
}
} // namespace

void serial_upload_boot_window() {
    bool requested=upload_latch==LATCH; upload_latch=0;
    if (!sdcar_is_ready()) {
        if(requested) Serial.println("@SDUP1 ERROR SD card unavailable");
        return;
    }
    buffer=(uint8_t *)heap_caps_malloc(SDUP_MAX_PAYLOAD,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    entries=(Entry *)heap_caps_calloc(MAX_ENTRIES,sizeof(Entry),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!buffer || !entries) {
        if(buffer) heap_caps_free(buffer); if(entries) heap_caps_free(entries);
        buffer=nullptr; entries=nullptr; Serial.println("@SDUP1 ERROR upload allocation failed"); return;
    }
    if (!recover_journal()) {
        Serial.println("@SDUP1 ERROR upload journal recovery failed; backups retained");
        heap_caps_free(buffer); heap_caps_free(entries); buffer=nullptr; entries=nullptr; return;
    }
    reset_entries();
    uint32_t started=millis();
    while (!requested && millis()-started<400) { requested=entry_command(); if(!requested) delay(2); }
    if (requested) receive_session();
    heap_caps_free(buffer); heap_caps_free(entries); buffer=nullptr; entries=nullptr;
}

void serial_upload_poll() {
    if (entry_command()) {
        Serial.println("@SDUP1 REBOOTING"); Serial.flush();
        upload_latch=LATCH; delay(100); ESP.restart();
    }
}
