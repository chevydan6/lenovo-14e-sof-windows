// SPDX-License-Identifier: MIT
#include "apo_abi.h"
#include <audioclient.h>
#include <tlhelp32.h>
#include <sddl.h>
#include <endpointvolume.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
class Media final:public IAudioMediaType {
 ULONG refs=1;
public:
 UINT32 rate=48000,channels=2;
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** p) override{if(!p)return E_POINTER;*p=this;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
 ULONG STDMETHODCALLTYPE Release() override{return --refs;}
 HRESULT STDMETHODCALLTYPE IsCompressedFormat(BOOL* p)override{if(!p)return E_POINTER;*p=FALSE;return S_OK;}
 HRESULT STDMETHODCALLTYPE IsEqual(IAudioMediaType*,DWORD* p)override{if(!p)return E_POINTER;*p=0;return S_OK;}
 const WAVEFORMATEX* STDMETHODCALLTYPE GetAudioFormat()override{return nullptr;}
 HRESULT STDMETHODCALLTYPE GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT* p)override{if(!p)return E_POINTER;*p={kFloat,channels,4,32,static_cast<float>(rate),3};return S_OK;}
};
static bool readAudioCounters(Telemetry* result){
 HANDLE ownToken=nullptr;
 if(OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&ownToken)){
  TOKEN_PRIVILEGES p{};p.PrivilegeCount=1;p.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
  if(LookupPrivilegeValueW(nullptr,L"SeDebugPrivilege",&p.Privileges[0].Luid))AdjustTokenPrivileges(ownToken,FALSE,&p,0,nullptr,nullptr);
  CloseHandle(ownToken);
 }
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
 if(!Process32FirstW(snapshot,&entry)){CloseHandle(snapshot);return false;}
 bool found=false;
 do{if(_wcsicmp(entry.szExeFile,L"audiodg.exe"))continue;
  HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,entry.th32ProcessID);MODULEENTRY32W module{};module.dwSize=sizeof(module);
  if(Module32FirstW(modules,&module)){do{
   if(_wcsicmp(module.szModule,L"lenovo14eapo.dll"))continue;
   HMODULE local=LoadLibraryExW(module.szExePath,nullptr,DONT_RESOLVE_DLL_REFERENCES);if(!local)continue;
   auto exported=GetProcAddress(local,"Lenovo14eApoTelemetry");
   if(exported){
    SIZE_T offset=reinterpret_cast<UINT_PTR>(exported)-reinterpret_cast<UINT_PTR>(local),read=0;
    HANDLE process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_INFORMATION,FALSE,entry.th32ProcessID);
    if(process){Telemetry sample{};bool valid=!!ReadProcessMemory(process,module.modBaseAddr+offset,&sample,sizeof(sample),&read)&&read==sizeof(sample);if(valid){if(!found||sample.frames>result->frames)*result=sample;found=true;}CloseHandle(process);}
   }
   FreeLibrary(local);if(found)break;
  }while(Module32NextW(modules,&module));}
  if(modules!=INVALID_HANDLE_VALUE)CloseHandle(modules);
 }while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return found;
}
static int status(){
 Telemetry data{};
 if(!readAudioCounters(&data)){std::puts("{\"available\":false}");return 2;}
 auto t=&data;
 std::printf("{\"available\":true,\"initialized\":%lld,\"locks\":%lld,\"calls\":%lld,\"frames\":%lld,\"silentFrames\":%lld,\"errors\":%lld,\"hostPid\":%ld,\"rate\":%ld,\"channels\":%ld,\"lastHr\":%ld,\"created\":%ld,\"registrationQueries\":%ld,\"formatQueries\":%ld,\"effectQueries\":%ld}\n",t->initialized,t->locks,t->calls,t->frames,t->silentFrames,t->errors,t->hostPid,t->lastRate,t->lastChannels,t->lastHr,t->created,t->registrationQueries,t->formatQueries,t->effectQueries);return 0;
}
static int audioToken(){
 HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
 if(!Process32FirstW(snapshot,&entry)){CloseHandle(snapshot);return 1;}
 do{if(_wcsicmp(entry.szExeFile,L"audiodg.exe"))continue;
  HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID),token=nullptr;
  if(!process||!OpenProcessToken(process,TOKEN_QUERY,&token)){std::printf("AudioDG token query error %lu\n",GetLastError());if(process)CloseHandle(process);continue;}
  std::printf("AudioDG PID %lu\n",entry.th32ProcessID);
  auto sid=[](PSID p){LPSTR text=nullptr;if(ConvertSidToStringSidA(p,&text)){std::printf("%s\n",text);LocalFree(text);}};
  for(auto kind:{TokenUser,TokenRestrictedSids,TokenGroups,TokenIntegrityLevel,TokenCapabilities}){
   DWORD size=0;GetTokenInformation(token,kind,nullptr,0,&size);std::vector<BYTE> data(size);
   if(!size||!GetTokenInformation(token,kind,data.data(),size,&size))continue;
   std::printf("Token information kind %u\n",static_cast<unsigned>(kind));
   if(kind==TokenUser)sid(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid);
   else if(kind==TokenIntegrityLevel)sid(reinterpret_cast<TOKEN_MANDATORY_LABEL*>(data.data())->Label.Sid);
   else{auto groups=reinterpret_cast<TOKEN_GROUPS*>(data.data());for(DWORD i=0;i<groups->GroupCount;++i)sid(groups->Groups[i].Sid);}
  }
  CloseHandle(token);CloseHandle(process);
 }while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return 0;
}
static int render(const wchar_t* endpoint,bool requireEffects=false){
 CHECK(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)));
 IMMDeviceEnumerator* e=nullptr;CHECK(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&e))));
 IMMDevice* device=nullptr;HRESULT hr=endpoint?e->GetDevice(endpoint,&device):e->GetDefaultAudioEndpoint(eRender,eConsole,&device);CHECK(SUCCEEDED(hr));
 LPWSTR endpointId=nullptr;if(SUCCEEDED(device->GetId(&endpointId))){std::printf("Endpoint: %ls\n",endpointId);CoTaskMemFree(endpointId);}
 IAudioClient* audio=nullptr;CHECK(SUCCEEDED(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&audio))));
 WAVEFORMATEX* f=nullptr;CHECK(SUCCEEDED(audio->GetMixFormat(&f)));std::printf("Mix format: rate=%lu channels=%u bits=%u tag=%u\n",f->nSamplesPerSec,f->nChannels,f->wBitsPerSample,f->wFormatTag);
 bool floating=f->wFormatTag==WAVE_FORMAT_IEEE_FLOAT || (f->wFormatTag==WAVE_FORMAT_EXTENSIBLE && IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(f)->SubFormat,kFloat));
 CHECK(floating && f->wBitsPerSample==32 && f->nChannels==2 && f->nSamplesPerSec==48000);
 hr=audio->Initialize(AUDCLNT_SHAREMODE_SHARED,0,1000000,0,f,nullptr);
 if(FAILED(hr))std::fprintf(stderr,"WASAPI Initialize HRESULT: 0x%08lx\n",static_cast<unsigned long>(hr));
 CHECK(SUCCEEDED(hr));
 IAudioRenderClient* rc=nullptr;CHECK(SUCCEEDED(audio->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void**>(&rc))));UINT32 capacity=0;CHECK(SUCCEEDED(audio->GetBufferSize(&capacity)));
 Telemetry before{};bool hadCounters=readAudioCounters(&before);
 UINT32 done=0;CHECK(SUCCEEDED(audio->Start()));
 while(done<144000){UINT32 pad=0;CHECK(SUCCEEDED(audio->GetCurrentPadding(&pad)));UINT32 n=std::min<UINT32>(capacity-pad,144000-done);if(!n){Sleep(4);continue;}BYTE* b=nullptr;CHECK(SUCCEEDED(rc->GetBuffer(n,&b)));auto v=reinterpret_cast<float*>(b);for(UINT32 i=0;i<n;++i){float sample=.001f*std::sin(6.283185307179586*200*(done+i)/48000);v[2*i]=v[2*i+1]=sample;}CHECK(SUCCEEDED(rc->ReleaseBuffer(n,0)));done+=n;}
 Sleep(200);status();Telemetry after{};bool hasCounters=readAudioCounters(&after);
 audio->Stop();rc->Release();audio->Release();device->Release();e->Release();CoTaskMemFree(f);CoUninitialize();
 if(requireEffects){
  LONG64 initial=hadCounters&&before.hostPid==after.hostPid?before.frames:0;
  CHECK(hasCounters && after.frames-initial>=144000 && after.errors==0 && after.lastRate==48000 && after.lastChannels==2);
  std::puts("PASS: AudioDG processed at least three seconds through the DSP with zero errors");
 }else std::puts("PASS: three seconds of -60 dBFS stereo WASAPI playback");return 0;
}
static int test(const wchar_t* path){
 auto m=LoadLibraryW(path);CHECK(m);
 using GetClass=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);auto get=reinterpret_cast<GetClass>(GetProcAddress(m,"DllGetClassObject"));CHECK(get);
 IClassFactory* factory=nullptr;CHECK(SUCCEEDED(get(kClsid,IID_IClassFactory,reinterpret_cast<void**>(&factory))));
 // AudioDG aggregates the APO; exported-factory tests without an outer
 // unknown do not exercise that activation path or its controlling identity.
 class Outer final:public IUnknown {
 public:
  ULONG refs=1;IUnknown* inner=nullptr;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** p) override {
   if(!p)return E_POINTER;*p=nullptr;
   if(IsEqualIID(iid,IID_IUnknown)){*p=this;AddRef();return S_OK;}
   return inner?inner->QueryInterface(iid,p):E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
  ULONG STDMETHODCALLTYPE Release() override{return --refs;}
 } outer;
 void* invalid=nullptr;
 CHECK(factory->CreateInstance(&outer,kApo,&invalid)==CLASS_E_NOAGGREGATION && !invalid);
 CHECK(SUCCEEDED(factory->CreateInstance(&outer,IID_IUnknown,reinterpret_cast<void**>(&outer.inner))));
 IAudioProcessingObject* aggregated=nullptr;
 CHECK(SUCCEEDED(outer.inner->QueryInterface(kApo,reinterpret_cast<void**>(&aggregated))) && outer.refs==2);
 IUnknown* identity=nullptr;
 CHECK(SUCCEEDED(aggregated->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity))) && identity==&outer && outer.refs==3);
 identity->Release();
 IAudioProcessingObjectRT* aggregatedRt=nullptr;
 CHECK(SUCCEEDED(aggregated->QueryInterface(kRt,reinterpret_cast<void**>(&aggregatedRt))) && outer.refs==3);
 aggregatedRt->Release();aggregated->Release();CHECK(outer.refs==1);
 CHECK(outer.inner->Release()==0);outer.inner=nullptr;
 IAudioProcessingObject* a=nullptr;CHECK(SUCCEEDED(factory->CreateInstance(nullptr,kApo,reinterpret_cast<void**>(&a))));factory->Release();
 IAudioProcessingObjectRT* rt=nullptr;IAudioProcessingObjectConfiguration* conf=nullptr;CHECK(SUCCEEDED(a->QueryInterface(kRt,reinterpret_cast<void**>(&rt))));CHECK(SUCCEEDED(a->QueryInterface(kConfig,reinterpret_cast<void**>(&conf))));
 CHECK(SUCCEEDED(a->Initialize(0,nullptr)));CHECK(FAILED(a->Initialize(0,nullptr)));
 APO_REG_PROPERTIES* props=nullptr;CHECK(SUCCEEDED(a->GetRegistrationProperties(&props)));CHECK(props->u32MinInputConnections==1&&props->Flags==15&&props->u32MajorVersion==1);CoTaskMemFree(props);
 IAudioSystemEffects2* effects=nullptr;CHECK(SUCCEEDED(a->QueryInterface(kEffects2,reinterpret_cast<void**>(&effects))));LPGUID effectIds=nullptr;UINT effectCount=0;CHECK(SUCCEEDED(effects->GetEffectsList(&effectIds,&effectCount,nullptr))&&effectCount==3);CoTaskMemFree(effectIds);effects->Release();
 HNSTIME latency=0;CHECK(SUCCEEDED(a->GetLatency(&latency))&&latency==30000);
 Media fmt;IAudioMediaType* supported=nullptr;CHECK(a->IsInputFormatSupported(nullptr,&fmt,&supported)==S_OK);fmt.rate=44100;CHECK(FAILED(a->IsInputFormatSupported(nullptr,&fmt,&supported)));fmt.rate=48000;fmt.channels=1;CHECK(FAILED(a->IsInputFormatSupported(nullptr,&fmt,&supported)));fmt.channels=2;
 float input[960]={},output[960]={};APO_CONNECTION_DESCRIPTOR di{APO_CONNECTION_BUFFER_TYPE_EXTERNAL,reinterpret_cast<UINT_PTR>(input),480,&fmt,0},doo{APO_CONNECTION_BUFFER_TYPE_EXTERNAL,reinterpret_cast<UINT_PTR>(output),480,&fmt,0};APO_CONNECTION_DESCRIPTOR* pin=&di;APO_CONNECTION_DESCRIPTOR* pout=&doo;
 CHECK(SUCCEEDED(conf->LockForProcess(1,&pin,1,&pout)));CHECK(FAILED(conf->LockForProcess(1,&pin,1,&pout)));CHECK(FAILED(a->Reset()));
 APO_CONNECTION_PROPERTY pi{reinterpret_cast<UINT_PTR>(input),480,BUFFER_VALID,0},po{reinterpret_cast<UINT_PTR>(output),0,BUFFER_INVALID,0};APO_CONNECTION_PROPERTY* ip=&pi;APO_CONNECTION_PROPERTY* op=&po;
 input[0]=4.0f;for(int j=0;j<40;++j){rt->APOProcess(1,&ip,1,&op);CHECK(po.u32ValidFrameCount==480);for(float v:output)CHECK(std::isfinite(v)&&std::fabs(v)<=.891251f+1e-6f);input[0]=0;}
 CHECK(std::fabs(output[1])<1e-12f); // channel isolation
 pi.u32BufferFlags=BUFFER_SILENT;pi.pBuffer=0;rt->APOProcess(1,&ip,1,&op);CHECK(po.u32ValidFrameCount==480);
 pi.u32ValidFrameCount=481;rt->APOProcess(1,&ip,1,&op);CHECK(po.u32BufferFlags==BUFFER_INVALID&&po.u32ValidFrameCount==0);
 CHECK(SUCCEEDED(conf->UnlockForProcess()));CHECK(FAILED(conf->UnlockForProcess()));CHECK(SUCCEEDED(a->Reset()));
 auto counters=reinterpret_cast<Telemetry*>(GetProcAddress(m,"Lenovo14eApoTelemetry"));CHECK(counters && counters->initialized==1 && counters->locks==1 && counters->calls==41 && counters->frames==19680);
 conf->Release();rt->Release();a->Release();FreeLibrary(m);std::puts("PASS: COM interfaces and aggregation, format negotiation, locks, processing, silence, bounds, latency and reset");return 0;
}
int wmain(int argc,wchar_t** argv){if(argc>=2&&!wcscmp(argv[1],L"--audio-token"))return audioToken();if(argc>=2&&!wcscmp(argv[1],L"--status"))return status();if(argc>=2&&!wcscmp(argv[1],L"--verify-render"))return render(argc>2?argv[2]:nullptr,true);if(argc>=2&&!wcscmp(argv[1],L"--render"))return render(argc>2?argv[2]:nullptr);return test(argc>1?argv[1]:L"lenovo14eapo.dll");}
