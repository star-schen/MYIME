#include "candidate_element.h"
#include <cstdio>
HMODULE g_module=nullptr;
long g_objects=0;
namespace {
struct Calls { int count=0,action=-99; unsigned long long generation=0; BOOL visible=FALSE; };
HRESULT action(void* owner,int index,unsigned long long generation) {
    auto& calls=*static_cast<Calls*>(owner);
    ++calls.count; calls.action=index; calls.generation=generation; return S_OK;
}
void visible(void* owner,BOOL show) { static_cast<Calls*>(owner)->visible=show; }
BOOL is_visible(void* owner) { return static_cast<Calls*>(owner)->visible; }
}
int main() {
    // Pure protocol/lifetime regression. No installed TIP, input injection,
    // game process, Rime userdb or UI automation is involved.
    bool passed=true; Calls calls;
    auto element=new CandidateElement(nullptr,{L"你好",L"您好"},0,&calls,action,visible,42,is_visible);
    UINT count=0,page=9,start=9,selection=9;
    passed &= element->GetCount(&count)==S_OK && count==2;
    passed &= element->GetCurrentPage(&page)==S_OK && page==0;
    passed &= element->GetPageIndex(nullptr,0,&count)==S_FALSE && count==1;
    passed &= element->GetPageIndex(&start,1,&count)==S_OK && start==0;
    UINT wrong[]={0,9}; passed &= element->SetPageIndex(wrong,2)==E_INVALIDARG;
    passed &= element->SetSelection(2)==E_INVALIDARG && calls.count==0;
    passed &= element->SetSelection(1)==S_OK && calls.count==0;
    passed &= element->GetSelection(&selection)==S_OK && selection==1;
    passed &= element->Finalize()==S_OK && calls.action==1 && calls.generation==42;
    passed &= element->Show(TRUE)==S_OK && calls.visible==TRUE;
    BOOL shown=FALSE;
    passed &= element->IsShown(&shown)==S_OK && shown;
    passed &= element->Show(FALSE)==S_OK && calls.visible==FALSE;
    element->update({L"你好",L"您好"},0,43);
    const int selected_before=calls.count;
    passed &= element->Finalize()==S_FALSE && calls.count==selected_before;
    passed &= element->SetSelection(0)==S_OK && element->Finalize()==S_OK && calls.generation==43;
    passed &= element->Abort()==S_OK && calls.action==-5;
    const int before=calls.count;
    element->detach(); // Simulates page change, focus change or service teardown.
    passed &= element->Finalize()==S_FALSE && element->Abort()==S_FALSE && calls.count==before;
    BSTR word=nullptr;
    passed &= element->GetString(1,&word)==S_OK && word && std::wstring(word)==L"您好";
    SysFreeString(word);
    passed &= element->GetString(2,&word)==E_INVALIDARG && word==nullptr;
    passed &= element->Release()==0 && g_objects==0;
    std::puts(passed?"Candidate snapshot/lifetime: PASS":"Candidate snapshot/lifetime: FAIL");
    return passed?0:1;
}
