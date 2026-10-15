// SPDX-License-Identifier: MIT
// One-shot elevated installer service. No driver or persistent service is added.
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include "registry_access.h"
static SERVICE_STATUS_HANDLE statusHandle;
static SERVICE_STATUS serviceStatus{};
static HANDLE stopEvent;
static std::wstring workerScript,workerName;
static void report(DWORD state,DWORD code=0){serviceStatus.dwServiceType=SERVICE_WIN32_OWN_PROCESS;serviceStatus.dwCurrentState=state;serviceStatus.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP:0;serviceStatus.dwWin32ExitCode=code?ERROR_SERVICE_SPECIFIC_ERROR:NO_ERROR;serviceStatus.dwServiceSpecificExitCode=code;SetServiceStatus(statusHandle,&serviceStatus);}
static DWORD WINAPI control(DWORD code,DWORD,void*,void*){if(code==SERVICE_CONTROL_STOP&&stopEvent)SetEvent(stopEvent);return NO_ERROR;}
static void WINAPI serviceMain(DWORD,wchar_t**){
 statusHandle=RegisterServiceCtrlHandlerExW(workerName.c_str(),control,nullptr);if(!statusHandle)return;
 stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);report(SERVICE_RUNNING);
 wchar_t windows[MAX_PATH];GetWindowsDirectoryW(windows,MAX_PATH);
 std::wstring app=std::wstring(windows)+L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
 std::wstring command=L"\""+app+L"\" -NoProfile -ExecutionPolicy Bypass -File \""+workerScript+L"\"";
 std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(0);
 STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};DWORD result=0;
 if(CreateProcessW(app.c_str(),buffer.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
  HANDLE handles[]={pi.hProcess,stopEvent};DWORD wait=WaitForMultipleObjects(2,handles,FALSE,100000);
  if(wait!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,ERROR_TIMEOUT);WaitForSingleObject(pi.hProcess,5000);result=ERROR_TIMEOUT;}
  else GetExitCodeProcess(pi.hProcess,&result);
  CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
 }else result=GetLastError();
 CloseHandle(stopEvent);stopEvent=nullptr;report(SERVICE_STOPPED,result);
}
static int run(const wchar_t* script,const wchar_t* name){
 wchar_t module[MAX_PATH],absolute[MAX_PATH],programs[MAX_PATH];
 if(!GetModuleFileNameW(nullptr,module,MAX_PATH)||!GetFullPathNameW(script,MAX_PATH,absolute,nullptr)||!GetEnvironmentVariableW(L"ProgramFiles",programs,MAX_PATH))return 1;
 std::wstring prefix=std::wstring(programs)+L"\\Lenovo14eAudio\\Installer\\";
 if(_wcsnicmp(absolute,prefix.c_str(),prefix.size())!=0||std::wstring(absolute).find(L"..")!=std::wstring::npos){std::fputs("Installer script must be in the protected installation directory.\n",stderr);return 1;}
 auto manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CREATE_SERVICE);if(!manager){std::fprintf(stderr,"OpenSCManager error %lu\n",GetLastError());return 1;}
 std::wstring command=L"\""+std::wstring(module)+L"\" --service \""+name+L"\" \""+absolute+L"\"";
 auto service=CreateServiceW(manager,name,L"Lenovo 14e temporary audio installer",SERVICE_ALL_ACCESS,SERVICE_WIN32_OWN_PROCESS,SERVICE_DEMAND_START,SERVICE_ERROR_NORMAL,command.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr);
 if(!service){std::fprintf(stderr,"CreateService error %lu\n",GetLastError());CloseServiceHandle(manager);return 1;}
 DWORD result=1;
 if(StartServiceW(service,0,nullptr)){
  DeleteService(service); // Removal completes even if the caller is interrupted.
  ULONGLONG start=GetTickCount64();SERVICE_STATUS_PROCESS state{};DWORD bytes=0;
  while(GetTickCount64()-start<110000){
   if(!QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&state),sizeof(state),&bytes))break;
   if(state.dwCurrentState==SERVICE_STOPPED){result=state.dwWin32ExitCode==ERROR_SERVICE_SPECIFIC_ERROR?state.dwServiceSpecificExitCode:state.dwWin32ExitCode;break;}
   Sleep(100);
  }
  if(result){SERVICE_STATUS ignored{};ControlService(service,SERVICE_CONTROL_STOP,&ignored);}
 }else std::fprintf(stderr,"StartService error %lu\n",GetLastError());
 DeleteService(service);CloseServiceHandle(service);CloseServiceHandle(manager);return static_cast<int>(result);
}
int wmain(int argc,wchar_t** argv){
 if(argc==4&&!wcscmp(argv[1],L"--unlock-registry"))return registryAccess(false,argv[2],argv[3]);
 if(argc==4&&!wcscmp(argv[1],L"--restore-registry"))return registryAccess(true,argv[2],argv[3]);
 if(argc==4&&!wcscmp(argv[1],L"--run"))return run(argv[2],argv[3]);
 if(argc==4&&!wcscmp(argv[1],L"--service")){workerName=argv[2];workerScript=argv[3];SERVICE_TABLE_ENTRYW table[]={{workerName.data(),serviceMain},{nullptr,nullptr}};return StartServiceCtrlDispatcherW(table)?0:1;}
 std::fputs("Usage: lenovo14e-apo-worker --run <protected installer script> <temporary service name>\n",stderr);return 1;
}
