// SPDX-License-Identifier: MIT
#pragma once
#include <aclapi.h>
#include <sddl.h>
static bool privilege(const wchar_t* name){
 HANDLE token; if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token))return false;
 TOKEN_PRIVILEGES p{};p.PrivilegeCount=1;p.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
 bool ok=!!LookupPrivilegeValueW(nullptr,name,&p.Privileges[0].Luid);
 if(ok){SetLastError(0);ok=!!AdjustTokenPrivileges(token,FALSE,&p,0,nullptr,nullptr)&&GetLastError()==0;}CloseHandle(token);return ok;
}
static bool registryTarget(const wchar_t* path){
 const std::wstring prefix=L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render\\";
 std::wstring p=path;
 if(p.size()<prefix.size()+40||_wcsnicmp(p.c_str(),prefix.c_str(),prefix.size()))return false;
 auto rest=p.substr(prefix.size());if(rest[0]!=L'{'||rest[37]!=L'}'||rest[38]!=L'\\')return false;
 auto tail=rest.substr(39);return !_wcsicmp(tail.c_str(),L"FxProperties")||!_wcsicmp(tail.c_str(),L"Properties");
}
static DWORD restoreSecurity(const wchar_t* path,const wchar_t* backup){
 HANDLE f=CreateFileW(backup,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return GetLastError();
 DWORD n=GetFileSize(f,nullptr),read=0;if(n>131072||n%2){CloseHandle(f);return ERROR_INVALID_DATA;}
 std::vector<wchar_t> text(n/2+1,0);BOOL ok=ReadFile(f,text.data(),n,&read,nullptr);CloseHandle(f);if(!ok||read!=n)return ERROR_READ_FAULT;
 PSECURITY_DESCRIPTOR sd=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(text.data(),SDDL_REVISION_1,&sd,nullptr))return GetLastError();
 PSID owner=nullptr,group=nullptr;PACL dacl=nullptr;BOOL ignored,present;SECURITY_DESCRIPTOR_CONTROL controlBits;DWORD revision;
 GetSecurityDescriptorOwner(sd,&owner,&ignored);GetSecurityDescriptorGroup(sd,&group,&ignored);GetSecurityDescriptorDacl(sd,&present,&dacl,&ignored);GetSecurityDescriptorControl(sd,&controlBits,&revision);
 SECURITY_INFORMATION info=OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION;
 info|=(controlBits&SE_DACL_PROTECTED)?PROTECTED_DACL_SECURITY_INFORMATION:UNPROTECTED_DACL_SECURITY_INFORMATION;
 std::wstring named=L"MACHINE\\"+std::wstring(path);
 DWORD e=SetNamedSecurityInfoW(named.data(),SE_REGISTRY_KEY,info,owner,group,dacl,nullptr);LocalFree(sd);return e;
}
static int registryAccess(bool restore,const wchar_t* path,const wchar_t* backup){
 if(!registryTarget(path)){std::fputs("Refusing access outside a render endpoint property store.\n",stderr);return 1;}
 if(!privilege(L"SeTakeOwnershipPrivilege")||!privilege(L"SeRestorePrivilege")){std::fputs("Required registry restoration privileges are unavailable.\n",stderr);return 1;}
 if(restore){DWORD e=restoreSecurity(path,backup);if(e)std::fprintf(stderr,"Registry security restore error %lu\n",e);return e?1:0;}
 std::wstring named=L"MACHINE\\"+std::wstring(path);PSECURITY_DESCRIPTOR sd=nullptr;PSID owner=nullptr,group=nullptr;PACL oldDacl=nullptr;
 DWORD e=GetNamedSecurityInfoW(named.data(),SE_REGISTRY_KEY,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,&group,&oldDacl,nullptr,&sd);
 if(e){std::fprintf(stderr,"Registry security read error %lu\n",e);return 1;}
 LPWSTR text=nullptr;
 if(!ConvertSecurityDescriptorToStringSecurityDescriptorW(sd,SDDL_REVISION_1,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&text,nullptr)){LocalFree(sd);return 1;}
 HANDLE f=CreateFileW(backup,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);DWORD written=0;
 if(f==INVALID_HANDLE_VALUE){LocalFree(text);LocalFree(sd);return 1;}
 DWORD n=static_cast<DWORD>(wcslen(text)*sizeof(wchar_t));BOOL saved=WriteFile(f,text,n,&written,nullptr);CloseHandle(f);LocalFree(text);
 if(!saved||written!=n){LocalFree(sd);return 1;}
 PSID system=nullptr;ConvertStringSidToSidW(L"S-1-5-18",&system);
 e=SetNamedSecurityInfoW(named.data(),SE_REGISTRY_KEY,OWNER_SECURITY_INFORMATION,system,nullptr,nullptr,nullptr);
 if(!e){
  EXPLICIT_ACCESSW grant{};grant.grfAccessPermissions=KEY_ALL_ACCESS;grant.grfAccessMode=GRANT_ACCESS;grant.grfInheritance=NO_INHERITANCE;
  grant.Trustee.TrusteeForm=TRUSTEE_IS_SID;grant.Trustee.TrusteeType=TRUSTEE_IS_USER;grant.Trustee.ptstrName=static_cast<LPWSTR>(system);
  PACL merged=nullptr;e=SetEntriesInAclW(1,&grant,oldDacl,&merged);
  if(!e)e=SetNamedSecurityInfoW(named.data(),SE_REGISTRY_KEY,DACL_SECURITY_INFORMATION,nullptr,nullptr,merged,nullptr);
  if(merged)LocalFree(merged);
 }
 LocalFree(system);LocalFree(sd);
 if(e){restoreSecurity(path,backup);std::fprintf(stderr,"Registry write-access error %lu\n",e);return 1;}
 return 0;
}
