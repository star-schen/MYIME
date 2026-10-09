// Storage adapter to official rime::Db. Ranking/learning formulas remain in
// upstream UserDictionary; its scoped wrapper invokes that same implementation.
#define MYIME_STORAGE_BUILD
#include "rime_storage.h"
#include <rime_api.h>
#include <rime/registry.h>
#include <rime/dict/user_db.h>
#include <rime/dict/level_db.h>
#include <rime/dict/tsv.h>
#include <rime/dict/db_utils.h>
#include <windows.h>
#include <bcrypt.h>
// SDK 26100 hides this typedef behind a Windows 11 gate but uses it in
// declarations available on Windows 10. Keep the original target and ABI.
#if defined(NTDDI_WIN11_ZN) && NTDDI_VERSION < NTDDI_WIN11_ZN
typedef const char* sqlite3_filename;
#endif
#include <winsqlite/winsqlite3.h>
#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <optional>
#include <stdexcept>
#include <vector>
#include <tuple>

namespace {
struct Event {
    uintptr_t session=0;
    std::string application,group="daily";
    bool habits=false,failed=false;
    int scope=-1,status=0;
};
thread_local Event event;
std::filesystem::path directory;
class SqliteDb;
thread_local std::set<SqliteDb*> writers;
thread_local std::set<SqliteDb*> readers;
std::string scope_id(){return event.scope==0?"":event.scope==1?event.group:event.habits?event.group:"";}
void failed(int code){event.failed=true;event.status=code?code:SQLITE_ERROR;}
std::string digest(const std::string& text){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD size=0,bytes=0;std::string output;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot initialize storage identity hash");
    struct Algorithm {BCRYPT_ALG_HANDLE value;~Algorithm(){BCryptCloseAlgorithmProvider(value,0);}} owner{algorithm};
    if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&bytes,0)<0)throw std::runtime_error("Cannot allocate storage hash");
    std::vector<UCHAR> object(size);UCHAR result[32]{};
    if(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)<0)throw std::runtime_error("Cannot create storage hash");
    struct Hash {BCRYPT_HASH_HANDLE value;~Hash(){BCryptDestroyHash(value);}} hashed{hash};
    if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),static_cast<ULONG>(text.size()),0)<0 || BCryptFinishHash(hash,result,sizeof(result),0)<0)throw std::runtime_error("Cannot hash storage identity");
    for(auto value:result){static const char hex[]="0123456789abcdef";output+=hex[value>>4];output+=hex[value&15];}return output;
}
class Statement {
    sqlite3_stmt* value_=nullptr;
public:
    Statement(sqlite3* db,const char* sql){const auto code=sqlite3_prepare_v2(db,sql,-1,&value_,nullptr);if(code!=SQLITE_OK){failed(code);throw std::runtime_error("Shared storage statement preparation failed");}}
    ~Statement(){sqlite3_finalize(value_);}
    Statement(const Statement&)=delete;
    void text(int index,const std::string& value){int code=sqlite3_bind_text(value_,index,value.data(),static_cast<int>(value.size()),SQLITE_TRANSIENT);if(code!=SQLITE_OK){failed(code);throw std::runtime_error("Shared storage parameter failed");}}
    void number(int index,sqlite3_int64 value){int code=sqlite3_bind_int64(value_,index,value);if(code!=SQLITE_OK){failed(code);throw std::runtime_error("Shared storage number failed");}}
    int step(){int code=sqlite3_step(value_);if(code!=SQLITE_ROW&&code!=SQLITE_DONE)failed(code);return code;}
    std::string text(int index)const{const auto value=sqlite3_column_text(value_,index);int size=sqlite3_column_bytes(value_,index);return value?std::string(reinterpret_cast<const char*>(value),size):std::string();}
    sqlite3_int64 number(int index)const{return sqlite3_column_int64(value_,index);}
};
class Accessor final : public rime::DbAccessor {
    sqlite3* db_;
    std::string scope_;
    bool metadata_,row_=false;
    std::unique_ptr<Statement> statement_;
    bool seek(const std::string& start){
        try {statement_.reset();statement_=std::make_unique<Statement>(db_,metadata_?"SELECT key,value FROM metadata WHERE scope=? AND key>=? ORDER BY key":"SELECT key,value FROM entries WHERE scope=? AND key>=? ORDER BY key");statement_->text(1,scope_);statement_->text(2,start);row_=statement_->step()==SQLITE_ROW;return !exhausted();}
        catch(...){row_=false;return false;}
    }
public:
    Accessor(sqlite3* db,std::string scope,bool metadata,std::string prefix):DbAccessor(prefix),db_(db),scope_(std::move(scope)),metadata_(metadata){Reset();}
    bool Reset()override{return seek(prefix_);}
    bool Jump(const std::string& key)override{return seek(key);}
    bool exhausted()override{return !row_ || statement_->text(0).compare(0,prefix_.size(),prefix_)!=0;}
    bool GetNextRecord(std::string* key,std::string* value)override{if(!key||!value||exhausted())return false;*key=statement_->text(0);*value=statement_->text(1);row_=statement_->step()==SQLITE_ROW;return true;}
};
class SqliteDb final : public rime::Db,public rime::Transactional {
    sqlite3* db_=nullptr;
    bool write_open_=false,read_open_=false;
    struct Undo {std::optional<std::string> before;sqlite3_int64 after_version=0;};
    std::map<std::pair<std::string,std::string>,Undo> undo_;
    struct TagUndo {sqlite3_int64 before=0,after=0;};
    std::map<std::tuple<std::string,std::string,std::string>,TagUndo> tag_undo_;
    bool execute(const char* sql){int code=sqlite3_exec(db_,sql,nullptr,nullptr,nullptr);if(code!=SQLITE_OK)failed(code);return code==SQLITE_OK;}
    std::optional<std::pair<std::string,sqlite3_int64>> record(const std::string& scope,const std::string& key){Statement query(db_,"SELECT value,version FROM entries WHERE scope=? AND key=?");query.text(1,scope);query.text(2,key);if(query.step()==SQLITE_ROW)return std::make_pair(query.text(0),query.number(1));return {};}
    bool metadata(const std::string& key){return !key.empty() && key.front()=='/';}
    std::string metadata_scope(const std::string& key){return key=="/tick"?scope_id():std::string();}
    sqlite3_int64 next_version(){
        Statement increment(db_,"UPDATE sequence SET value=value+1 WHERE id=1");
        if(increment.step()!=SQLITE_DONE)throw std::runtime_error("Cannot allocate shared storage version");
        Statement query(db_,"SELECT value FROM sequence WHERE id=1");
        if(query.step()!=SQLITE_ROW)throw std::runtime_error("Cannot read shared storage version");
        return query.number(0);
    }
    sqlite3_int64 tag_uses(const std::tuple<std::string,std::string,std::string>& key){
        Statement query(db_,"SELECT uses FROM tags WHERE group_id=? AND application=? AND key=?");
        query.text(1,std::get<0>(key));query.text(2,std::get<1>(key));query.text(3,std::get<2>(key));
        return query.step()==SQLITE_ROW?query.number(0):0;
    }
    void clear_undo(){undo_.clear();tag_undo_.clear();in_transaction_=false;}
    bool restore_undo(){
        if(!begin_write())return false;
        try {
            for(const auto& item:undo_){const auto current=record(item.first.first,item.first.second);if(!current || current->second!=item.second.after_version){finish(false);clear_undo();event.status=SQLITE_BUSY;return false;}}
            for(const auto& item:tag_undo_)if(tag_uses(item.first)!=item.second.after){finish(false);clear_undo();event.status=SQLITE_BUSY;return false;}
            for(const auto& item:undo_){
                if(item.second.before){Statement update(db_,"UPDATE entries SET value=?,version=? WHERE scope=? AND key=?");update.text(1,*item.second.before);update.number(2,next_version());update.text(3,item.first.first);update.text(4,item.first.second);if(update.step()!=SQLITE_DONE)return false;}
                else{Statement erase(db_,"DELETE FROM entries WHERE scope=? AND key=?");erase.text(1,item.first.first);erase.text(2,item.first.second);if(erase.step()!=SQLITE_DONE)return false;}
            }
            for(const auto& item:tag_undo_){Statement update(db_,item.second.before?"UPDATE tags SET uses=? WHERE group_id=? AND application=? AND key=?":"DELETE FROM tags WHERE uses>=? AND group_id=? AND application=? AND key=?");update.number(1,item.second.before);update.text(2,std::get<0>(item.first));update.text(3,std::get<1>(item.first));update.text(4,std::get<2>(item.first));if(update.step()!=SQLITE_DONE)return false;}
            clear_undo();return true;
        }catch(...){failed(SQLITE_ERROR);return false;}
    }
public:
    SqliteDb(const std::string& name):Db(rime::path(directory/(digest(name)+".sqlite3")),name){}
    ~SqliteDb()override{Close();}
    bool begin_read(){if(write_open_||read_open_)return true;if(!db_)return false;if(!execute("BEGIN"))return false;read_open_=true;readers.insert(this);return true;}
    bool end_read(){if(!read_open_)return true;const auto result=execute("COMMIT");read_open_=false;readers.erase(this);return result;}
    bool begin_write(){if(write_open_)return true;if(!db_||readonly_||!end_read())return false;if(!execute("BEGIN IMMEDIATE"))return false;write_open_=true;writers.insert(this);return true;}
    bool finish(bool commit){if(!write_open_)return true;bool ok=execute(commit?"COMMIT":"ROLLBACK");if(!ok && commit)sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);write_open_=false;writers.erase(this);if(!commit || !ok)clear_undo();return ok;}
    bool Open()override{
        if(loaded_)return true;
        int code=sqlite3_open16(file_path_.c_str(),&db_);if(code!=SQLITE_OK){failed(code);Close();return false;}
        sqlite3_busy_timeout(db_,12);
        if(!execute("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA cache_size=-128; CREATE TABLE IF NOT EXISTS entries(scope TEXT NOT NULL,key TEXT NOT NULL,value TEXT NOT NULL,version INTEGER NOT NULL DEFAULT 1,PRIMARY KEY(scope,key)) WITHOUT ROWID; CREATE TABLE IF NOT EXISTS metadata(scope TEXT NOT NULL,key TEXT NOT NULL,value TEXT NOT NULL,PRIMARY KEY(scope,key)) WITHOUT ROWID; CREATE TABLE IF NOT EXISTS tags(group_id TEXT NOT NULL,application TEXT NOT NULL,key TEXT NOT NULL,uses INTEGER NOT NULL,PRIMARY KEY(group_id,application,key)) WITHOUT ROWID; CREATE TABLE IF NOT EXISTS sequence(id INTEGER PRIMARY KEY,value INTEGER NOT NULL); INSERT OR IGNORE INTO sequence(id,value) SELECT 1,COALESCE(MAX(version),0) FROM entries;")){Close();return false;}
        loaded_=true;readonly_=false;
        if(!begin_write() || !CreateMetadata() || !finish(true)){Close();return false;}return true;
    }
    bool OpenReadOnly()override{if(!Open())return false;readonly_=true;return true;}
    bool Close()override{if(!db_)return true;end_read();finish(!event.failed);sqlite3_close_v2(db_);db_=nullptr;loaded_=readonly_=false;return true;}
    bool CreateMetadata()override{return MetaUpdate("/db_name",name_)&&MetaUpdate("/db_type","userdb")&&MetaUpdate("/rime_version","1.17.0")&&rime::UserDbHelper(this).UpdateUserInfo();}
    bool MetaFetch(const std::string& key,std::string* value)override{if(!value||!db_||(key=="/tick"&&!begin_read()))return false;try{Statement query(db_,"SELECT value FROM metadata WHERE scope=? AND key=?");query.text(1,metadata_scope(key));query.text(2,key);if(query.step()!=SQLITE_ROW)return false;*value=query.text(0);return true;}catch(...){return false;}}
    bool MetaUpdate(const std::string& key,const std::string& value)override{if(!begin_write())return false;try{Statement update(db_,"INSERT INTO metadata(scope,key,value) VALUES(?,?,?) ON CONFLICT(scope,key) DO UPDATE SET value=excluded.value");update.text(1,metadata_scope(key));update.text(2,key);update.text(3,value);return update.step()==SQLITE_DONE;}catch(...){return false;}}
    rime::an<rime::DbAccessor> QueryMetadata()override{if(!begin_read())return {};return std::make_shared<Accessor>(db_,"",true,"");}
    rime::an<rime::DbAccessor> QueryAll()override{return Query("");}
    rime::an<rime::DbAccessor> Query(const std::string& key)override{if(!begin_read())return {};return std::make_shared<Accessor>(db_,scope_id(),false,key);}
    bool Fetch(const std::string& key,std::string* value)override{if(metadata(key))return MetaFetch(key,value);if(!value||!db_)return false;try{auto result=record(scope_id(),key);if(!result)return false;*value=result->first;return true;}catch(...){return false;}}
    bool Update(const std::string& key,const std::string& value)override{
        if(metadata(key))return MetaUpdate(key,value);if(!begin_write())return false;
        try {const auto scope=scope_id();const auto previous=in_transaction_?record(scope,key):std::optional<std::pair<std::string,sqlite3_int64>>{};
            const auto version=next_version();
            Statement update(db_,"INSERT INTO entries(scope,key,value,version) VALUES(?,?,?,?) ON CONFLICT(scope,key) DO UPDATE SET value=excluded.value,version=excluded.version");update.text(1,scope);update.text(2,key);update.text(3,value);update.number(4,version);if(update.step()!=SQLITE_DONE)return false;
            if(in_transaction_){auto& undo=undo_[{scope,key}];if(!undo.after_version)undo.before=previous?std::optional<std::string>(previous->first):std::nullopt;undo.after_version=version;}
            if(event.scope==1 && !event.application.empty()){
                const auto identity=std::make_tuple(event.group,event.application,key);const auto before=tag_uses(identity);
                Statement tag(db_,"INSERT INTO tags(group_id,application,key,uses) VALUES(?,?,?,1) ON CONFLICT(group_id,application,key) DO UPDATE SET uses=tags.uses+1");tag.text(1,event.group);tag.text(2,event.application);tag.text(3,key);if(tag.step()!=SQLITE_DONE)return false;
                if(in_transaction_){auto& undo=tag_undo_[identity];if(!undo.after)undo.before=before;undo.after=before+1;}
            }return true;
        }catch(...){return false;}
    }
    bool Erase(const std::string& key)override{if(!begin_write())return false;try{Statement erase(db_,"DELETE FROM entries WHERE scope=? AND key=?");erase.text(1,scope_id());erase.text(2,key);return erase.step()==SQLITE_DONE;}catch(...){return false;}}
    bool BeginTransaction()override{if(!begin_write())return false;clear_undo();in_transaction_=true;return true;}
    bool AbortTransaction()override{return restore_undo();}
    bool CommitTransaction()override{clear_undo();return true;}
    bool Backup(const rime::path& target)override{return rime::UserDbHelper(this).UniformBackup(target);}
    bool Restore(const rime::path& source)override{return rime::UserDbHelper(this).UniformRestore(source);}
    bool Remove()override{return false;} // Offline removal must explicitly cover every namespace.
};
class Factory final : public rime::UserDb::Component {
public:
    std::string extension()const override{return ".sqlite3";}
    // An explicitly disabled shared store has no userdb. Upstream translators
    // retain the deployed dictionary; never redirect to a slot or relative file.
    rime::Db* Create(const std::string& name)override{if(directory.empty())return nullptr;try{return new SqliteDb(name);}catch(...){failed(SQLITE_CANTOPEN);return nullptr;}}
};
void initialize(){rime::Registry::instance().Register("userdb",new Factory);}
void finalize(){}
}
namespace myime_storage {
bool active(rime::Db* db){return dynamic_cast<SqliteDb*>(db)!=nullptr;}
std::string pool_key(const std::string& name,const std::string& db_class){return db_class=="userdb"?name+"@myime-session-"+std::to_string(event.session):name;}
Scope::Scope(int scope):previous_(event.scope){event.scope=scope;}
Scope::~Scope(){event.scope=previous_;}
WriteGuard::WriteGuard(rime::Db* db):ready_(!active(db)||static_cast<SqliteDb*>(db)->begin_write()){}
WriteGuard::~WriteGuard(){if(!ready_||!complete_)failed(SQLITE_ERROR);}
}
extern "C" {
int MyimeStorageConfigure(const wchar_t* root){
    try{
        if(!root)return 0;
        if(!*root){directory.clear();return 1;}
        auto path=std::filesystem::path(root);
        if(!path.is_absolute() || sqlite3_libversion_number()<3024000 || !std::filesystem::is_directory(path))return 0;
        directory=path;return 1;
    }catch(...){return 0;}
}
void MyimeStorageSession(uintptr_t id,const char* application,const char* group,int habits){
    try{event.session=id;event.failed=false;event.status=0;event.application=application?application:"";event.group=group&&*group?group:"daily";event.habits=habits!=0;event.scope=-1;}catch(...){failed(SQLITE_NOMEM);}
}
int MyimeStorageFinishEvent(int success){
    bool ok=true;while(!readers.empty()){auto* db=*readers.begin();if(!db->end_read())ok=false;}
    const bool commit=success&&!event.failed;while(!writers.empty()){auto* db=*writers.begin();if(!db->finish(commit))ok=false;}
    // Intentional rollback is successful cleanup, not a learning failure.
    return ok && !event.failed && event.status==0;
}
int MyimeStorageStatus(){return event.status;}
int MyimeStorageImportSlot(const wchar_t* root){
    try {
        if(!root || !std::filesystem::path(root).is_absolute())return -1;
        HANDLE lease=CreateFileW((std::filesystem::path(root)/L".host.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(lease==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_SHARING_VIOLATION)return -2;return -1;}
        struct Lease {HANDLE value;~Lease(){CloseHandle(value);}} owned{lease};int merged=0;
        for(const auto& entry:std::filesystem::directory_iterator(root)){
            if(!entry.is_directory() || entry.path().extension()!=L".userdb" || merged>=128)continue;
            const auto attributes=GetFileAttributesW(entry.path().c_str());if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return -1;
            const auto name=entry.path().stem().u8string();
            rime::UserDbWrapper<rime::LevelDb> original(rime::path(entry.path()),name);
            if(!original.OpenReadOnly())continue;
            SqliteDb target(name);if(!target.Open() || !target.begin_write())return -1;
            for(int scope:{0,1}){myime_storage::Scope selected(scope);rime::UserDbMerger sink(&target);rime::DbSource source(&original);source.Dump(&sink);sink.CloseMerge();}
            if(event.failed || !target.finish(true))return -1;merged++;
        }
        return merged;
    }catch(...){MyimeStorageFinishEvent(0);return -1;}
}
int MyimeStorageMaintain(int operation,const wchar_t* argument){
    if(operation==1)return MyimeStorageImportSlot(argument);
    if(operation!=2 && operation!=3)return -1;
    try{
        std::filesystem::path target;
        if(operation==2){if(!argument || !std::filesystem::path(argument).is_absolute())return -1;target=argument;if(std::filesystem::exists(target))return -1;std::filesystem::create_directories(target);}
        int count=0;
        for(const auto& entry:std::filesystem::directory_iterator(directory)){
            if(!entry.is_regular_file() || entry.path().extension()!=L".sqlite3")continue;
            sqlite3* source=nullptr;sqlite3* copy=nullptr;
            struct Connections{sqlite3*& source;sqlite3*& copy;~Connections(){if(copy)sqlite3_close_v2(copy);if(source)sqlite3_close_v2(source);}} owned{source,copy};
            if(sqlite3_open16(entry.path().c_str(),&source)!=SQLITE_OK)return -1;
            sqlite3_busy_timeout(source,1000);
            if(operation==3){int frames=0,finished=0;int code=sqlite3_wal_checkpoint_v2(source,nullptr,SQLITE_CHECKPOINT_PASSIVE,&frames,&finished);if(code!=SQLITE_OK && code!=SQLITE_BUSY)return -1;if(code==SQLITE_OK)count++;continue;}
            const auto destination=target/entry.path().filename();
            if(sqlite3_open16(destination.c_str(),&copy)!=SQLITE_OK)return -1;
            sqlite3_backup* backup=sqlite3_backup_init(copy,"main",source,"main");if(!backup)return -1;
            int code=SQLITE_OK;unsigned attempts=0;
            while((code=sqlite3_backup_step(backup,256))==SQLITE_OK || code==SQLITE_BUSY || code==SQLITE_LOCKED){if(++attempts>600){code=SQLITE_BUSY;break;}if(code==SQLITE_BUSY || code==SQLITE_LOCKED)Sleep(50);}
            const int ended=sqlite3_backup_finish(backup);if(code!=SQLITE_DONE || ended!=SQLITE_OK)return -1;count++;
        }
        return count;
    }catch(...){return -1;}
}
}
static void rime_myime_storage_initialize(){initialize();}
static void rime_myime_storage_finalize(){finalize();}
RIME_REGISTER_MODULE(myime_storage)
