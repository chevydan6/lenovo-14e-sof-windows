// SPDX-License-Identifier: MIT
#include "apo_abi.h"
#include "chromebook_dsp.h"
#include <sddl.h>
#include <new>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <algorithm>
static HMODULE moduleHandle;
static volatile LONG objects=0,serverLocks=0;
static bool same(REFGUID a,REFGUID b) {return !!IsEqualGUID(a,b);}
static void properties(APO_REG_PROPERTIES* p) {
 std::memset(p,0,sizeof(*p));p->clsid=kClsid;p->Flags=static_cast<APO_FLAG>(15);
 std::wcscpy(p->szFriendlyName,L"Lenovo 14e Gen 3 Speaker Processing");
 std::wcscpy(p->szCopyrightInfo,L"Independent open-source speaker processing; MIT");
 p->u32MajorVersion=0;p->u32MinorVersion=3;
 p->u32MinInputConnections=p->u32MaxInputConnections=1;
 p->u32MinOutputConnections=p->u32MaxOutputConnections=1;
 p->u32MaxInstances=0xffffffff;p->u32NumAPOInterfaces=1;p->iidAPOInterfaceList[0]=kApo;
}
static bool format(IAudioMediaType* t) {
 UNCOMPRESSEDAUDIOFORMAT f{};
 return t && SUCCEEDED(t->GetUncompressedAudioFormat(&f)) && same(f.guidFormatType,kFloat) &&
  f.dwSamplesPerFrame==2 && f.dwBytesPerSampleContainer==4 && f.dwValidBitsPerSample==32 &&
  f.fFramesPerSecond==48000 && (f.dwChannelMask==3 || f.dwChannelMask==0);
}
class LenovoApo final : public IAudioProcessingObject,public IAudioProcessingObjectRT,
 public IAudioProcessingObjectConfiguration,public IAudioSystemEffects2 {
 volatile LONG references=1;
 bool initialized=false,locked=false,pinned=false;
 UINT32 maximumFrames=0;
 cb_state state{};
 HANDLE mapping=nullptr;
 Telemetry* telemetry=nullptr;
 void connectTelemetry() {
  PSECURITY_DESCRIPTOR sd=nullptr;
  ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;LS)(A;;GR;;;AU)",SDDL_REVISION_1,&sd,nullptr);
  SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
  mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,sd?&sa:nullptr,PAGE_READWRITE,0,sizeof(Telemetry),kTelemetryName);
  if(sd)LocalFree(sd);
  if(mapping)telemetry=static_cast<Telemetry*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Telemetry)));
 }
 HRESULT failed(HRESULT hr) {if(telemetry){InterlockedIncrement64(&telemetry->errors);InterlockedExchange(&telemetry->lastHr,hr);}return hr;}
public:
 LenovoApo(){InterlockedIncrement(&objects);}
 ~LenovoApo(){if(pinned)VirtualUnlock(&state,sizeof(state));if(telemetry)UnmapViewOfFile(telemetry);if(mapping)CloseHandle(mapping);InterlockedDecrement(&objects);}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** pp) override {
  if(!pp)return E_POINTER;*pp=nullptr;
  if(same(iid,IID_IUnknown)||same(iid,kApo))*pp=static_cast<IAudioProcessingObject*>(this);
  else if(same(iid,kRt))*pp=static_cast<IAudioProcessingObjectRT*>(this);
  else if(same(iid,kConfig))*pp=static_cast<IAudioProcessingObjectConfiguration*>(this);
  else if(same(iid,kEffects)||same(iid,kEffects2))*pp=static_cast<IAudioSystemEffects2*>(this);
  else return E_NOINTERFACE;
  AddRef();return S_OK;
 }
 ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&references);}
 ULONG STDMETHODCALLTYPE Release() override {LONG n=InterlockedDecrement(&references);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Initialize(UINT32 bytes,BYTE* data) override {
  if(initialized)return static_cast<HRESULT>(0x887d0001);
  if(bytes && (!data || bytes<sizeof(APOInitBaseStruct)))return E_INVALIDARG;
  if(bytes && !same(reinterpret_cast<APOInitBaseStruct*>(data)->clsid,kClsid))return static_cast<HRESULT>(0x887d0004);
  connectTelemetry();if(telemetry){InterlockedIncrement64(&telemetry->initialized);InterlockedExchange(&telemetry->hostPid,GetCurrentProcessId());}
  cb_config c;cb_measured_config(&c,50);
  // EFX receives Windows' already volume-scaled samples. Remove the fixed
  // 50%-capture reference, NOT the user's actual volume attenuation.
  c.input_gain_db=23.5;
  // Preview headroom, independent of the retained kernel amplifier watchdog.
  c.output_gain_db-=6;c.limiter_ceiling_db=-6;
  if(cb_init(&state,&c))return failed(E_FAIL);
  initialized=true;return S_OK;
 }
 HRESULT STDMETHODCALLTYPE Reset() override {if(locked)return kLocked;cb_reset(&state);return S_OK;}
 HRESULT STDMETHODCALLTYPE GetLatency(HNSTIME* t) override {if(!t)return E_POINTER;*t=30000;return S_OK;}
 HRESULT STDMETHODCALLTYPE GetRegistrationProperties(APO_REG_PROPERTIES** p) override {
  if(!p)return E_POINTER;*p=static_cast<APO_REG_PROPERTIES*>(CoTaskMemAlloc(sizeof(APO_REG_PROPERTIES)));
  if(!*p)return E_OUTOFMEMORY;properties(*p);return S_OK;
 }
 HRESULT support(IAudioMediaType* opposite,IAudioMediaType* requested,IAudioMediaType** supported) {
  if(!supported)return E_POINTER;*supported=nullptr;
  if(locked)return kLocked;
  if(!format(requested)||(opposite&&!format(opposite)))return kFormatError;
  return S_OK;
 }
 HRESULT STDMETHODCALLTYPE IsInputFormatSupported(IAudioMediaType* a,IAudioMediaType* b,IAudioMediaType** c) override{return support(a,b,c);}
 HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(IAudioMediaType* a,IAudioMediaType* b,IAudioMediaType** c) override{return support(a,b,c);}
 HRESULT STDMETHODCALLTYPE GetInputChannelCount(UINT32* p) override{if(!p)return E_POINTER;*p=2;return S_OK;}
 HRESULT STDMETHODCALLTYPE GetEffectsList(LPGUID* ids,UINT* count,HANDLE) override {
  if(!ids||!count)return E_POINTER;*ids=nullptr;*count=0;
  auto p=static_cast<GUID*>(CoTaskMemAlloc(sizeof(GUID)));if(!p)return E_OUTOFMEMORY;*p=kClsid;*ids=p;*count=1;return S_OK;
 }
 HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 ni,APO_CONNECTION_DESCRIPTOR** in,UINT32 no,APO_CONNECTION_DESCRIPTOR** out) override {
  if(locked)return kLocked;if(!initialized)return failed(static_cast<HRESULT>(0x887d0002));
  if(ni!=1||no!=1||!in||!out||!in[0]||!out[0])return failed(E_INVALIDARG);
  if(!format(in[0]->pFormat)||!format(out[0]->pFormat))return failed(kFormatError);
  if(!in[0]->u32MaxFrameCount||in[0]->u32MaxFrameCount>48000||out[0]->u32MaxFrameCount<in[0]->u32MaxFrameCount)return failed(E_INVALIDARG);
  maximumFrames=in[0]->u32MaxFrameCount;cb_reset(&state);
  pinned=!!VirtualLock(&state,sizeof(state));locked=true;
  if(telemetry){InterlockedIncrement64(&telemetry->locks);InterlockedExchange(&telemetry->lastRate,48000);InterlockedExchange(&telemetry->lastChannels,2);}
  return S_OK;
 }
 HRESULT STDMETHODCALLTYPE UnlockForProcess() override {
  if(!locked)return static_cast<HRESULT>(0x887d0006);
  locked=false;if(pinned){VirtualUnlock(&state,sizeof(state));pinned=false;}return S_OK;
 }
 UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 n) override{return n;}
 UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 n) override{return n;}
 void STDMETHODCALLTYPE APOProcess(UINT32 ni,APO_CONNECTION_PROPERTY** in,UINT32 no,APO_CONNECTION_PROPERTY** out) override {
  if(no!=1||!out||!out[0])return;
  auto o=out[0];o->u32ValidFrameCount=0;o->u32BufferFlags=BUFFER_INVALID;
  if(!locked||ni!=1||!in||!in[0]||!o->pBuffer||in[0]->u32ValidFrameCount>maximumFrames){if(telemetry)InterlockedIncrement64(&telemetry->errors);return;}
  auto i=in[0];UINT32 n=i->u32ValidFrameCount;
  if(i->u32BufferFlags==BUFFER_INVALID)return;
  float* output=reinterpret_cast<float*>(o->pBuffer);
  if(i->u32BufferFlags==BUFFER_SILENT){
   static const float zero[512]={};UINT32 pos=0;
   while(pos<n){UINT32 chunk=std::min<UINT32>(256,n-pos);cb_process_f32(&state,zero,output+2*pos,chunk);pos+=chunk;}
   if(telemetry)InterlockedExchangeAdd64(&telemetry->silentFrames,n);
  }else if(i->u32BufferFlags==BUFFER_VALID&&i->pBuffer){
   cb_process_f32(&state,reinterpret_cast<const float*>(i->pBuffer),output,n);
  }else return;
  bool silent=true;for(UINT32 k=0;k<2*n;++k){if(std::fabs(output[k])>1e-12f){silent=false;break;}}
  o->u32ValidFrameCount=n;o->u32BufferFlags=silent?BUFFER_SILENT:BUFFER_VALID;
  if(telemetry){InterlockedIncrement64(&telemetry->calls);InterlockedExchangeAdd64(&telemetry->frames,n);}
 }
};
class Factory final : public IClassFactory {
 volatile LONG refs=1;
public:
 Factory(){InterlockedIncrement(&objects);}~Factory(){InterlockedDecrement(&objects);}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** p) override{if(!p)return E_POINTER;*p=nullptr;if(!same(iid,IID_IUnknown)&&!same(iid,IID_IClassFactory))return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
 ULONG STDMETHODCALLTYPE Release() override{LONG n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** p) override{if(!p)return E_POINTER;*p=nullptr;if(outer)return CLASS_E_NOAGGREGATION;auto obj=new(std::nothrow) LenovoApo;if(!obj)return E_OUTOFMEMORY;HRESULT hr=obj->QueryInterface(iid,p);obj->Release();return hr;}
 HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override{if(lock)InterlockedIncrement(&serverLocks);else InterlockedDecrement(&serverLocks);return S_OK;}
};
STDAPI DllGetClassObject(REFCLSID clsid,REFIID iid,void** p){if(!same(clsid,kClsid))return CLASS_E_CLASSNOTAVAILABLE;auto f=new(std::nothrow) Factory;if(!f)return E_OUTOFMEMORY;HRESULT hr=f->QueryInterface(iid,p);f->Release();return hr;}
STDAPI DllCanUnloadNow(){return objects||serverLocks?S_FALSE:S_OK;}
BOOL WINAPI DllMain(HINSTANCE m,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){moduleHandle=m;DisableThreadLibraryCalls(m);}return TRUE;}
static LSTATUS str(HKEY key,const WCHAR* name,const WCHAR* value){return RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value),static_cast<DWORD>((wcslen(value)+1)*sizeof(WCHAR)));}
STDAPI DllRegisterServer(){
 HMODULE self=moduleHandle;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&DllRegisterServer),&self);
 WCHAR path[MAX_PATH];if(!GetModuleFileNameW(self,path,MAX_PATH))return HRESULT_FROM_WIN32(GetLastError());
 WCHAR key[512];HKEY h;swprintf(key,512,L"SOFTWARE\\Classes\\CLSID\\%ls\\InprocServer32",kClsidText);
 LSTATUS e=RegCreateKeyExW(HKEY_LOCAL_MACHINE,key,0,nullptr,0,KEY_WRITE,nullptr,&h,nullptr);if(e)return HRESULT_FROM_WIN32(e);
 e=str(h,nullptr,path);if(!e)e=str(h,L"ThreadingModel",L"Both");RegCloseKey(h);if(e)return HRESULT_FROM_WIN32(e);
 swprintf(key,512,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio\\AudioProcessingObjects\\%ls",kClsidText);
 e=RegCreateKeyExW(HKEY_LOCAL_MACHINE,key,0,nullptr,0,KEY_WRITE,nullptr,&h,nullptr);if(e)return HRESULT_FROM_WIN32(e);
 e=str(h,L"FriendlyName",L"Lenovo 14e Gen 3 Speaker Processing");
 const WCHAR* names[]={L"MajorVersion",L"MinorVersion",L"Flags",L"MinInputConnections",L"MaxInputConnections",L"MinOutputConnections",L"MaxOutputConnections",L"MaxInstances",L"NumAPOInterfaces"};
 DWORD values[]={0,3,15,1,1,1,1,0xffffffff,1};
 for(unsigned i=0;i<9&&!e;++i)e=RegSetValueExW(h,names[i],0,REG_DWORD,reinterpret_cast<const BYTE*>(&values[i]),sizeof(DWORD));
 if(!e)e=RegSetValueExW(h,L"APOInterface0",0,REG_SZ,reinterpret_cast<const BYTE*>(L"{FD7F2B29-24D0-4B5C-B177-592C39F9CA10}"),39*sizeof(WCHAR));
 RegCloseKey(h);return HRESULT_FROM_WIN32(e);
}
STDAPI DllUnregisterServer(){WCHAR key[512];swprintf(key,512,L"SOFTWARE\\Classes\\CLSID\\%ls",kClsidText);RegDeleteTreeW(HKEY_LOCAL_MACHINE,key);swprintf(key,512,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio\\AudioProcessingObjects\\%ls",kClsidText);RegDeleteTreeW(HKEY_LOCAL_MACHINE,key);return S_OK;}
