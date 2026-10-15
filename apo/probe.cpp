// SPDX-License-Identifier: MIT
#include "apo_abi.h"
#include <audioclient.h>
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
static int status(){
 HANDLE h=nullptr;
 auto path=telemetryPath();HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file!=INVALID_HANDLE_VALUE){h=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,sizeof(Telemetry),nullptr);CloseHandle(file);}
 if(!h)h=OpenFileMappingW(FILE_MAP_READ,FALSE,kTelemetryName);
 if(!h){std::puts("{\"available\":false}");return 2;}
 auto t=static_cast<const Telemetry*>(MapViewOfFile(h,FILE_MAP_READ,0,0,sizeof(Telemetry)));if(!t){CloseHandle(h);return 2;}
 std::printf("{\"available\":true,\"initialized\":%lld,\"locks\":%lld,\"calls\":%lld,\"frames\":%lld,\"silentFrames\":%lld,\"errors\":%lld,\"hostPid\":%ld,\"rate\":%ld,\"channels\":%ld,\"lastHr\":%ld}\n",t->initialized,t->locks,t->calls,t->frames,t->silentFrames,t->errors,t->hostPid,t->lastRate,t->lastChannels,t->lastHr);
 UnmapViewOfFile(t);CloseHandle(h);return 0;
}
static int render(const wchar_t* endpoint){
 CHECK(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)));
 IMMDeviceEnumerator* e=nullptr;CHECK(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&e))));
 IMMDevice* device=nullptr;HRESULT hr=endpoint?e->GetDevice(endpoint,&device):e->GetDefaultAudioEndpoint(eRender,eConsole,&device);CHECK(SUCCEEDED(hr));
 IAudioClient* audio=nullptr;CHECK(SUCCEEDED(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&audio))));
 WAVEFORMATEX* f=nullptr;CHECK(SUCCEEDED(audio->GetMixFormat(&f)));std::printf("Mix format: rate=%lu channels=%u bits=%u tag=%u\n",f->nSamplesPerSec,f->nChannels,f->wBitsPerSample,f->wFormatTag);
 bool floating=f->wFormatTag==WAVE_FORMAT_IEEE_FLOAT || (f->wFormatTag==WAVE_FORMAT_EXTENSIBLE && IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(f)->SubFormat,kFloat));
 CHECK(floating && f->wBitsPerSample==32 && f->nChannels==2 && f->nSamplesPerSec==48000);
 CHECK(SUCCEEDED(audio->Initialize(AUDCLNT_SHAREMODE_SHARED,0,1000000,0,f,nullptr)));
 IAudioRenderClient* rc=nullptr;CHECK(SUCCEEDED(audio->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void**>(&rc))));UINT32 capacity=0;CHECK(SUCCEEDED(audio->GetBufferSize(&capacity)));
 UINT32 done=0;CHECK(SUCCEEDED(audio->Start()));
 while(done<144000){UINT32 pad=0;CHECK(SUCCEEDED(audio->GetCurrentPadding(&pad)));UINT32 n=std::min<UINT32>(capacity-pad,144000-done);if(!n){Sleep(4);continue;}BYTE* b=nullptr;CHECK(SUCCEEDED(rc->GetBuffer(n,&b)));auto v=reinterpret_cast<float*>(b);for(UINT32 i=0;i<n;++i){float sample=.001f*std::sin(6.283185307179586*200*(done+i)/48000);v[2*i]=v[2*i+1]=sample;}CHECK(SUCCEEDED(rc->ReleaseBuffer(n,0)));done+=n;}
 Sleep(200);status();audio->Stop();rc->Release();audio->Release();device->Release();e->Release();CoTaskMemFree(f);CoUninitialize();std::puts("PASS: three seconds of -60 dBFS stereo WASAPI playback");return 0;
}
static int test(const wchar_t* path){
 auto m=LoadLibraryW(path);CHECK(m);
 using GetClass=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);auto get=reinterpret_cast<GetClass>(GetProcAddress(m,"DllGetClassObject"));CHECK(get);
 IClassFactory* factory=nullptr;CHECK(SUCCEEDED(get(kClsid,IID_IClassFactory,reinterpret_cast<void**>(&factory))));
 IAudioProcessingObject* a=nullptr;CHECK(SUCCEEDED(factory->CreateInstance(nullptr,kApo,reinterpret_cast<void**>(&a))));factory->Release();
 IAudioProcessingObjectRT* rt=nullptr;IAudioProcessingObjectConfiguration* conf=nullptr;CHECK(SUCCEEDED(a->QueryInterface(kRt,reinterpret_cast<void**>(&rt))));CHECK(SUCCEEDED(a->QueryInterface(kConfig,reinterpret_cast<void**>(&conf))));
 CHECK(SUCCEEDED(a->Initialize(0,nullptr)));CHECK(FAILED(a->Initialize(0,nullptr)));
 APO_REG_PROPERTIES* props=nullptr;CHECK(SUCCEEDED(a->GetRegistrationProperties(&props)));CHECK(props->u32MinInputConnections==1&&props->Flags==15);CoTaskMemFree(props);
 HNSTIME latency=0;CHECK(SUCCEEDED(a->GetLatency(&latency))&&latency==30000);
 Media fmt;IAudioMediaType* supported=nullptr;CHECK(a->IsInputFormatSupported(nullptr,&fmt,&supported)==S_OK);fmt.rate=44100;CHECK(FAILED(a->IsInputFormatSupported(nullptr,&fmt,&supported)));fmt.rate=48000;fmt.channels=1;CHECK(FAILED(a->IsInputFormatSupported(nullptr,&fmt,&supported)));fmt.channels=2;
 float input[960]={},output[960]={};APO_CONNECTION_DESCRIPTOR di{APO_CONNECTION_BUFFER_TYPE_EXTERNAL,reinterpret_cast<UINT_PTR>(input),480,&fmt,0},doo{APO_CONNECTION_BUFFER_TYPE_EXTERNAL,reinterpret_cast<UINT_PTR>(output),480,&fmt,0};APO_CONNECTION_DESCRIPTOR* pin=&di;APO_CONNECTION_DESCRIPTOR* pout=&doo;
 CHECK(SUCCEEDED(conf->LockForProcess(1,&pin,1,&pout)));CHECK(FAILED(conf->LockForProcess(1,&pin,1,&pout)));CHECK(FAILED(a->Reset()));
 APO_CONNECTION_PROPERTY pi{reinterpret_cast<UINT_PTR>(input),480,BUFFER_VALID,0},po{reinterpret_cast<UINT_PTR>(output),0,BUFFER_INVALID,0};APO_CONNECTION_PROPERTY* ip=&pi;APO_CONNECTION_PROPERTY* op=&po;
 input[0]=.001f;for(int j=0;j<40;++j){rt->APOProcess(1,&ip,1,&op);CHECK(po.u32ValidFrameCount==480);for(float v:output)CHECK(std::isfinite(v)&&std::fabs(v)<=.501188f+1e-6f);input[0]=0;}
 CHECK(std::fabs(output[1])<1e-12f); // channel isolation
 pi.u32BufferFlags=BUFFER_SILENT;pi.pBuffer=0;rt->APOProcess(1,&ip,1,&op);CHECK(po.u32ValidFrameCount==480);
 pi.u32ValidFrameCount=481;rt->APOProcess(1,&ip,1,&op);CHECK(po.u32BufferFlags==BUFFER_INVALID&&po.u32ValidFrameCount==0);
 CHECK(SUCCEEDED(conf->UnlockForProcess()));CHECK(FAILED(conf->UnlockForProcess()));CHECK(SUCCEEDED(a->Reset()));
 conf->Release();rt->Release();a->Release();FreeLibrary(m);std::puts("PASS: COM interfaces, format negotiation, locks, processing, silence, bounds, latency and reset");return 0;
}
int wmain(int argc,wchar_t** argv){if(argc>=2&&!wcscmp(argv[1],L"--status"))return status();if(argc>=2&&!wcscmp(argv[1],L"--render"))return render(argc>2?argv[2]:nullptr);return test(argc>1?argv[1]:L"lenovo14eapo.dll");}
