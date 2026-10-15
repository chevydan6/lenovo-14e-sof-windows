// SPDX-License-Identifier: MIT
#pragma once
// Minimal declarations of the documented Windows APO ABI, for builds with
// MinGW (which does not ship audioenginebaseapo.h). Not an alternative API.
// Layout and method order checked against Microsoft's published SDK headers.
#include <windows.h>
#include <string>
#include <unknwn.h>
#include <mmreg.h>
#include <audioapotypes.h>
#include <propsys.h>
#include <mmdeviceapi.h>
#if !defined(__MINGW32__)
#include <audioenginebaseapo.h>
#include <audiomediatype.h>
#else
struct UNCOMPRESSEDAUDIOFORMAT {
 GUID guidFormatType; DWORD dwSamplesPerFrame, dwBytesPerSampleContainer;
 DWORD dwValidBitsPerSample; FLOAT fFramesPerSecond; DWORD dwChannelMask;
};
struct IAudioMediaType : IUnknown {
 virtual HRESULT STDMETHODCALLTYPE IsCompressedFormat(BOOL*)=0;
 virtual HRESULT STDMETHODCALLTYPE IsEqual(IAudioMediaType*,DWORD*)=0;
 virtual const WAVEFORMATEX* STDMETHODCALLTYPE GetAudioFormat()=0;
 virtual HRESULT STDMETHODCALLTYPE GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT*)=0;
};
enum APO_CONNECTION_BUFFER_TYPE { APO_CONNECTION_BUFFER_TYPE_ALLOCATED, APO_CONNECTION_BUFFER_TYPE_EXTERNAL, APO_CONNECTION_BUFFER_TYPE_DEPENDANT };
struct APO_CONNECTION_DESCRIPTOR { APO_CONNECTION_BUFFER_TYPE Type; UINT_PTR pBuffer; UINT32 u32MaxFrameCount; IAudioMediaType* pFormat; UINT32 u32Signature; };
enum APO_FLAG { APO_FLAG_NONE=0, APO_FLAG_INPLACE=1, APO_FLAG_DEFAULT=14 };
struct APO_REG_PROPERTIES {
 CLSID clsid; APO_FLAG Flags; WCHAR szFriendlyName[256],szCopyrightInfo[256];
 UINT32 u32MajorVersion,u32MinorVersion,u32MinInputConnections,u32MaxInputConnections;
 UINT32 u32MinOutputConnections,u32MaxOutputConnections,u32MaxInstances,u32NumAPOInterfaces;
 IID iidAPOInterfaceList[1];
};
struct APOInitBaseStruct { UINT32 cbSize; CLSID clsid; };
struct APOInitSystemEffects { APOInitBaseStruct APOInit; IPropertyStore* pAPOEndpointProperties; IPropertyStore* pAPOSystemEffectsProperties; void* pReserved; IMMDeviceCollection* pDeviceCollection; };
struct IAudioProcessingObject : IUnknown {
 virtual HRESULT STDMETHODCALLTYPE Reset()=0;
 virtual HRESULT STDMETHODCALLTYPE GetLatency(HNSTIME*)=0;
 virtual HRESULT STDMETHODCALLTYPE GetRegistrationProperties(APO_REG_PROPERTIES**)=0;
 virtual HRESULT STDMETHODCALLTYPE Initialize(UINT32,BYTE*)=0;
 virtual HRESULT STDMETHODCALLTYPE IsInputFormatSupported(IAudioMediaType*,IAudioMediaType*,IAudioMediaType**)=0;
 virtual HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(IAudioMediaType*,IAudioMediaType*,IAudioMediaType**)=0;
 virtual HRESULT STDMETHODCALLTYPE GetInputChannelCount(UINT32*)=0;
};
struct IAudioProcessingObjectRT : IUnknown {
 virtual void STDMETHODCALLTYPE APOProcess(UINT32,APO_CONNECTION_PROPERTY**,UINT32,APO_CONNECTION_PROPERTY**)=0;
 virtual UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32)=0;
 virtual UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32)=0;
};
struct IAudioProcessingObjectConfiguration : IUnknown {
 virtual HRESULT STDMETHODCALLTYPE LockForProcess(UINT32,APO_CONNECTION_DESCRIPTOR**,UINT32,APO_CONNECTION_DESCRIPTOR**)=0;
 virtual HRESULT STDMETHODCALLTYPE UnlockForProcess()=0;
};
struct IAudioSystemEffects : IUnknown {};
struct IAudioSystemEffects2 : IAudioSystemEffects {
 virtual HRESULT STDMETHODCALLTYPE GetEffectsList(LPGUID*,UINT*,HANDLE)=0;
};
#endif
inline constexpr GUID kClsid={0xc324c041,0x34ec,0x4d9a,{0x9c,0xc8,0xb0,0x65,0x14,0xe0,0x00,0x01}};
inline constexpr GUID kApo={0xfd7f2b29,0x24d0,0x4b5c,{0xb1,0x77,0x59,0x2c,0x39,0xf9,0xca,0x10}};
inline constexpr GUID kRt={0x9e1d6a6d,0xddbc,0x4e95,{0xa4,0xc7,0xad,0x64,0xba,0x37,0x84,0x6c}};
inline constexpr GUID kConfig={0x0e5ed805,0xaba6,0x49c3,{0x8f,0x9a,0x2b,0x8c,0x88,0x9c,0x4f,0xa8}};
inline constexpr GUID kEffects={0x5fa00f27,0xadd6,0x499a,{0x8a,0x9d,0x6b,0x98,0x52,0x1f,0xa7,0x5b}};
inline constexpr GUID kEffects2={0xbafe99d2,0x7436,0x44ce,{0x9e,0x0e,0x4d,0x89,0xaf,0xbf,0xff,0x56}};
inline constexpr GUID kFloat={3,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
inline constexpr HRESULT kFormatError=static_cast<HRESULT>(0x887d0003);
inline constexpr HRESULT kLocked=static_cast<HRESULT>(0x887d000a);
inline constexpr WCHAR kClsidText[]=L"{C324C041-34EC-4D9A-9CC8-B06514E00001}";
inline constexpr WCHAR kTelemetryName[]=L"Global\\Lenovo14eSofApoTelemetryV1";
inline std::wstring telemetryPath() {
 wchar_t data[MAX_PATH];DWORD n=GetEnvironmentVariableW(L"ProgramData",data,MAX_PATH);
 return n&&n<MAX_PATH?std::wstring(data)+L"\\Lenovo14eAudio\\telemetry.bin":std::wstring();
}
struct alignas(8) Telemetry {
 volatile LONG64 initialized,locks,calls,frames,silentFrames,errors;
 volatile LONG hostPid,lastRate,lastChannels,lastHr;
};
static_assert(sizeof(Telemetry)==64,"Shared counter file ABI");
static_assert(sizeof(APO_CONNECTION_DESCRIPTOR)==40,"APO descriptor x64 ABI");
static_assert(sizeof(APO_CONNECTION_PROPERTY)==24,"APO property x64 ABI");
static_assert(sizeof(UNCOMPRESSEDAUDIOFORMAT)==36,"Audio media ABI");
