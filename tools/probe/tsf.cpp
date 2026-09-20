#include "text_store.h"
#include "ids.h"
#include "manual_dispatch.h"
#include <cstdio>
int main(int argc,char**) {
    const bool reject_test=argc>1;
    SetEnvironmentVariableW(L"MYIME_DIAGNOSTICS",L"1");
    if (FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 1;
    HMODULE dll=LoadLibraryW(L"myime_host.dll"); if (!dll) return 2;
    auto create=reinterpret_cast<HRESULT(WINAPI*)(REFCLSID,REFIID,void**)>(GetProcAddress(dll,"DllGetClassObject"));
    bool passed=true;
    {
        ComPtr<ITfThreadMgrEx> manager; TfClientId client=TF_CLIENTID_NULL;
        HRESULT hr=CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager));
        std::printf("CoCreate thread manager: %08lx\n",hr);
        if (SUCCEEDED(hr)) hr=manager->ActivateEx(&client,TF_TMAE_NOACTIVATETIP);
        std::printf("Activate thread: %08lx client=%lu\n",hr,client);
        ComPtr<ITfDocumentMgr> document; if (SUCCEEDED(hr)) hr=manager->CreateDocumentMgr(&document);
        ComPtr<TextStore> store; store.Attach(new TextStore);
        ComPtr<ITfContext> context; TfEditCookie cookie=0;
        if (SUCCEEDED(hr)) hr=document->CreateContext(client,0,static_cast<ITextStoreACP*>(store.Get()),&context,&cookie);
        std::printf("Create context: %08lx\n",hr);
        if (SUCCEEDED(hr)) hr=document->Push(context.Get());
        if (SUCCEEDED(hr)) hr=manager->SetFocus(document.Get());
        std::printf("Focus document: %08lx\n",hr);
        ComPtr<IClassFactory> factory; if (SUCCEEDED(hr)) hr=create(kService,IID_PPV_ARGS(&factory));
        ComPtr<ITfTextInputProcessorEx> service; if (SUCCEEDED(hr)) hr=factory->CreateInstance(nullptr,IID_PPV_ARGS(&service));
        ComPtr<ManualDispatch> dispatch; dispatch.Attach(new ManualDispatch(manager.Get()));
        if (SUCCEEDED(hr)) hr=service->ActivateEx(dispatch.Get(),client,0);
        std::printf("TSF activation: 0x%08lx\n",static_cast<unsigned long>(hr));
        ComPtr<ITfKeyEventSink> keys; if (SUCCEEDED(hr)) hr=service.As(&keys);
        passed=SUCCEEDED(hr);
        if (passed) {
            for (wchar_t key:std::wstring(L"NIHAO")) {
                BOOL eaten=FALSE;
                const auto before=store->text;
                keys->OnTestKeyDown(context.Get(),key,0,&eaten);
                passed &= eaten!=FALSE && store->text==before;
                keys->OnTestKeyDown(context.Get(),key,0,&eaten);
                passed &= store->text==before; // Repeated test callbacks cannot advance Rime.
                keys->OnKeyDown(context.Get(),key,0,&eaten); passed &= eaten!=FALSE;
                keys->OnKeyUp(context.Get(),key,0,&eaten);
            }
            passed &= store->starts==1 && store->ends==0 && !store->text.empty();
            BOOL eaten=FALSE; keys->OnKeyDown(context.Get(),VK_SPACE,0,&eaten);
            passed &= eaten!=FALSE && store->text==L"你好" && store->ends==1;
            std::printf("TSF document commit: %s; starts=%d ends=%d\n",store->text==L"你好"?"PASS":"FAIL",store->starts,store->ends);
            store->read_only=true;
            keys->OnTestKeyDown(context.Get(),'N',0,&eaten); passed &= !eaten;
            store->read_only=false;
            keys->OnKeyDown(context.Get(),'N',0,&eaten); passed &= eaten!=FALSE;
            keys->OnKeyDown(context.Get(),VK_ESCAPE,0,&eaten);
            std::printf("Cancel: length=%zu starts=%d ends=%d\n",store->text.size(),store->starts,store->ends);
            passed &= store->text==L"你好" && store->starts==2 && store->ends==2;
            // The application may end a composition independently of the IME.
            keys->OnKeyDown(context.Get(),'N',0,&eaten);
            ComPtr<ITfContextOwnerCompositionServices> owner;
            auto terminate=context.As(&owner);
            if (SUCCEEDED(terminate)) terminate=owner->TerminateComposition(nullptr);
            passed &= SUCCEEDED(terminate) && store->ends==3;
            const auto terminated_text=store->text;
            keys->OnKeyDown(context.Get(),'N',0,&eaten);
            passed &= eaten!=FALSE && store->starts==4;
            keys->OnKeyDown(context.Get(),VK_ESCAPE,0,&eaten);
            passed &= store->text==terminated_text && store->ends==4;
            store->type(L'\b'); // Remove the raw preedit preserved by app termination.
            passed &= store->text==L"你好";
            if (reject_test) {
            store->reject_composition=true;
            auto before_refs=service->AddRef(); service->Release();
            keys->OnKeyDown(context.Get(),'N',0,&eaten);
            std::printf("Reject: length=%zu starts=%d ends=%d\n",store->text.size(),store->starts,store->ends);
            passed &= store->text==L"你好"; // A rejected composition must not insert raw preedit.
            auto after_refs=service->AddRef(); service->Release();
            auto end_refs=service->AddRef(); service->Release();
            std::printf("Service refs before/after rejection: %lu/%lu\n",before_refs,after_refs);
            passed &= before_refs==after_refs && end_refs==after_refs;
            }
            service->Deactivate();
        }
        if (document) document->Pop(TF_POPF_ALL);
        if (manager) manager->Deactivate();
    }
    // TSF may defer releasing rejected composition objects to its message queue.
    MSG msg{}; while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    CoUninitialize();
    auto unload=reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(dll,"DllCanUnloadNow"));
    const auto unload_result=unload?unload():E_FAIL;
    std::printf("Unload: 0x%08lx\n",unload_result);
    // A refused composition can retain the independent no-op sink in TSF.
    // The DLL must stay mapped until Windows releases it; the service is freed.
    if (!reject_test) passed &= unload_result==S_OK;
    if (unload_result==S_OK) FreeLibrary(dll);
    std::puts(passed?"TSF integration: PASS":"TSF integration: FAIL"); return passed?0:3;
}
