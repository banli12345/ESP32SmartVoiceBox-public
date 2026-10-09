// Compile the actual receiver with deterministic UART/SD failures.
#define SDCAR_H
inline bool sdcar_is_ready(){return true;}
#include "../src/SerialUpload.cpp"
#include <iostream>
static unsigned checks=0,failures=0;
#define CHECK(c) do { ++checks; if(!(c)){++failures;std::cerr<<"FAIL line "<<__LINE__<<'\n';} } while(0)
using Bytes=std::vector<uint8_t>;
static void initialize(){
    if(!buffer)buffer=(uint8_t *)std::malloc(SDUP_MAX_PAYLOAD);
    if(!entries)entries=(Entry *)std::calloc(MAX_ENTRIES,sizeof(Entry));
    reset_entries();SD_MMC=FakeSD{};Serial=FakeSerial{};fake_ms=0;File::partial_done=false;
}
static Bytes bytes(const char *p){return Bytes(p,p+std::strlen(p));}
static void u16(Bytes &p,uint16_t v){p.push_back(v&255);p.push_back(v>>8);}
static void u32(Bytes &p,uint32_t v){for(int i=0;i<4;++i)p.push_back((v>>(i*8))&255);}
static Bytes manifest(){
    Bytes p;u16(p,2);
    for(auto path:{"/videos/a.rvf","/videos/a.wav"}){
        u16(p,std::strlen(path));u32(p,3);u32(p,sdup_crc32(0,"new",3));
        p.insert(p.end(),path,path+std::strlen(path));
    }
    return p;
}
static Bytes data(unsigned index){Bytes p;u16(p,index);u32(p,0);auto b=bytes("new");p.insert(p.end(),b.begin(),b.end());return p;}
static Bytes index(unsigned i){Bytes p;u16(p,i);return p;}
static void request(unsigned op,unsigned seq,const Bytes &payload={}){
    Bytes frame={'S','D','U','1',(uint8_t)op,0};u16(frame,payload.size());u32(frame,seq);
    frame.insert(frame.end(),payload.begin(),payload.end());u32(frame,sdup_crc32(0,frame.data()+4,frame.size()-4));
    Serial.input.insert(Serial.input.end(),frame.begin(),frame.end());
}
static void old_files(){SD_MMC.files["/videos/a.rvf"]=bytes("old-video");SD_MMC.files["/videos/a.wav"]=bytes("old-audio");}
static void prepare_pair(){
    auto m=manifest();CHECK(!begin_batch(m.data(),m.size()));
    for(unsigned i=0;i<2;++i){auto d=data(i);CHECK(!write_chunk(d.data(),d.size()));auto n=index(i);CHECK(!finish_file(n.data(),n.size(),i+1));}
}
static void originals(){CHECK(SD_MMC.files["/videos/a.rvf"]==bytes("old-video"));CHECK(SD_MMC.files["/videos/a.wav"]==bytes("old-audio"));}
int main(){
    initialize();old_files();request(SDUP_PING,1);request(SDUP_BEGIN,2,manifest());
    request(SDUP_DATA,3,data(0));request(SDUP_DATA,3,data(0)); // lost ACK retry
    request(SDUP_FINISH,4,index(0));request(SDUP_DATA,5,data(1));request(SDUP_FINISH,6,index(1));
    request(SDUP_COMMIT,7);request(SDUP_COMMIT,7);request(SDUP_EXIT,8);
    try{receive_session();CHECK(false);}catch(Reboot&){}
    CHECK(SD_MMC.files.size()==2);CHECK(SD_MMC.files["/videos/a.rvf"]==bytes("new"));CHECK(SD_MMC.files["/videos/a.wav"]==bytes("new"));
    initialize();old_files();request(SDUP_BEGIN,1,manifest());request(SDUP_DATA,2,data(0));request(SDUP_ABORT,0);
    try{receive_session();CHECK(false);}catch(Reboot&){} originals();CHECK(SD_MMC.files.size()==2);
    initialize();old_files();auto m=manifest();CHECK(!begin_batch(m.data(),m.size()));auto d=data(0);CHECK(!write_chunk(d.data(),d.size()));close_current();
    SD_MMC.files["/videos/a.rvf.sdup-part"][0]^=1;auto n=index(0);CHECK(finish_file(n.data(),n.size(),1)!=nullptr);originals();
    initialize();old_files();prepare_pair();SD_MMC.fail_rename_to="/videos/a.wav";CHECK(commit_batch()!=nullptr);originals();CHECK(!SD_MMC.exists(JOURNAL));
    // Power loss after each rename, including after both new files exist.
    for(int step=1;step<=4;++step){
        initialize();old_files();prepare_pair();
        SD_MMC.crash_after_rename=SD_MMC.rename_number+step+1; // journal installation is the first rename
        bool crashed=false;try{commit_batch();}catch(Crash&){crashed=true;}
        CHECK(crashed);CHECK(recover_journal());originals();CHECK(SD_MMC.files.size()==2);
    }
    initialize();old_files();prepare_pair();File::partial_done=true;CHECK(commit_batch()!=nullptr);originals();CHECK(SD_MMC.files.size()==2);
    initialize();old_files();prepare_pair();SD_MMC.crash_backup_cleanup=true;
    bool crashed=false;try{commit_batch();}catch(Crash&){crashed=true;}
    CHECK(crashed);CHECK(recover_journal());CHECK(SD_MMC.files.size()==2);
    CHECK(SD_MMC.files["/videos/a.rvf"]==bytes("new"));CHECK(SD_MMC.files["/videos/a.wav"]==bytes("new"));
    // A silent replacement must also remove a stale companion in the batch.
    initialize();old_files();Bytes single;u16(single,1);const char *path="/videos/a.rvf";
    u16(single,std::strlen(path));u32(single,3);u32(single,sdup_crc32(0,"new",3));single.insert(single.end(),path,path+std::strlen(path));
    CHECK(!begin_batch(single.data(),single.size()));d=data(0);CHECK(!write_chunk(d.data(),d.size()));n=index(0);CHECK(!finish_file(n.data(),n.size(),1));CHECK(!commit_batch());
    CHECK(SD_MMC.files["/videos/a.rvf"]==bytes("new"));CHECK(!SD_MMC.exists("/videos/a.wav"));
    std::cout<<"receiver: "<<checks<<" checks, "<<failures<<" failures\n";
    std::free(buffer);std::free(entries);return failures?1:0;
}
