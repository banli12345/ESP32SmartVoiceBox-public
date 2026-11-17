#pragma once
#include "Arduino.h"
#include <map>
#include <set>
#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"
struct Crash {};
struct File {
    std::vector<uint8_t> *data=nullptr; size_t pos=0; bool valid=false,dir=false;
    bool journal=false;
    static inline bool partial_done=false;
    explicit operator bool()const{return valid;}
    bool isDirectory()const{return dir;}
    size_t size()const{return data?data->size():0;}
    size_t read(uint8_t *p,size_t n){
        if(!data)return 0;
        n=std::min(n,data->size()-pos);std::memcpy(p,data->data()+pos,n);pos+=n;return n;
    }
    size_t write(const uint8_t *p,size_t n){
        if(!data)return 0;
        if(journal && partial_done && n==4 && !std::memcmp(p,"DONE",4)){n=2;partial_done=false;}
        if(pos+n>data->size())data->resize(pos+n);
        std::memcpy(data->data()+pos,p,n);pos+=n;return n;
    }
    void flush(){}
    void close(){valid=false;data=nullptr;}
    void setBufferSize(size_t){}
};
struct FakeSD {
    std::map<std::string,std::vector<uint8_t>> files;
    std::set<std::string> dirs{"/"};
    int rename_number=0,crash_after_rename=0;
    std::string fail_rename_to;
    bool crash_backup_cleanup=false;
    bool exists(const char *p){return files.count(p)||dirs.count(p);}
    bool mkdir(const char *p){dirs.insert(p);return true;}
    File open(const char *p,const char *mode=FILE_READ){
        File f; f.journal=std::string(p)=="/.sdup-journal";
        if(dirs.count(p)){f.valid=f.dir=true;return f;}
        if(!std::strcmp(mode,FILE_READ)){
            auto it=files.find(p);if(it==files.end())return f;
            f.data=&it->second;
        }else f.data=&files[p];
        f.valid=true;
        if(!std::strcmp(mode,FILE_APPEND))f.pos=f.data->size();
        return f;
    }
    bool remove(const char *p){
        std::string path=p;
        if(crash_backup_cleanup && path.ends_with(".sdup-bak")){crash_backup_cleanup=false;throw Crash{};}
        return files.erase(p)>0;
    }
    bool rename(const char *a,const char *b){
        if(fail_rename_to==b){fail_rename_to.clear();return false;}
        if(!files.count(a)||exists(b))return false;
        files[b]=std::move(files[a]);files.erase(a);
        ++rename_number;
        if(crash_after_rename && rename_number==crash_after_rename){crash_after_rename=0;throw Crash{};}
        return true;
    }
    uint64_t totalBytes(){return 1024ull*1024*1024;}
    uint64_t usedBytes(){uint64_t n=0;for(auto &p:files)n+=p.second.size();return n;}
};
inline FakeSD SD_MMC;
