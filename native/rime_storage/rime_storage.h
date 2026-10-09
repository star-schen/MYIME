#pragma once
#include <stddef.h>
#include <stdint.h>
#if defined(MYIME_STORAGE_BUILD)
#define MYIME_STORAGE_API __declspec(dllexport)
#else
#define MYIME_STORAGE_API __declspec(dllimport)
#endif
/* Additive C boundary inside the pinned librime build. No C++ objects cross
 * the DLL boundary. Calls use the same serialized runtime as official Rime.
 */
extern "C" {
/* Empty directory explicitly disables userdb without creating a private store.
 * Only configure while no native sessions are alive, under the runtime lock. */
MYIME_STORAGE_API int MyimeStorageConfigure(const wchar_t* directory);
MYIME_STORAGE_API void MyimeStorageSession(uintptr_t id,const char* application,const char* group,int habits);
/* success=0 rolls back; a completed rollback without storage errors succeeds. */
MYIME_STORAGE_API int MyimeStorageFinishEvent(int success);
MYIME_STORAGE_API int MyimeStorageStatus(void);
MYIME_STORAGE_API int MyimeStorageImportSlot(const wchar_t* slot);
/* operation: 1 merge a closed legacy slot, 2 online backup to a new directory,
 * 3 passive WAL checkpoint. Returns count; -2 means a busy legacy slot. */
MYIME_STORAGE_API int MyimeStorageMaintain(int operation,const wchar_t* argument);
}
#if defined(MYIME_STORAGE_BUILD)
#include <rime/dict/db.h>
namespace myime_storage {
bool active(rime::Db*);
std::string pool_key(const std::string&,const std::string& db_class);
class Scope {
    int previous_;
public:
    explicit Scope(int scope);
    ~Scope();
};
class WriteGuard {
    bool ready_,complete_=false;
public:
    explicit WriteGuard(rime::Db*);
    ~WriteGuard();
    explicit operator bool() const{return ready_;}
    void complete(bool value){complete_=value;}
};
}
#endif
