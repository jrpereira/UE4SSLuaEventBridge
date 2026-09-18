// Host-side test launcher. Uses a separate desktop; never calls SwitchDesktop.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fstream>
#include <string>
#include <iostream>
static BOOL CALLBACK window_record(HWND hwnd, LPARAM output) {
    DWORD pid=0; GetWindowThreadProcessId(hwnd,&pid);
    wchar_t title[256]{}; GetWindowTextW(hwnd,title,256);
    auto& file=*reinterpret_cast<std::wofstream*>(output);
    RECT bounds{}; GetWindowRect(hwnd,&bounds);
    if(IsWindowVisible(hwnd) && std::wstring(title)==L"Windows Sandbox" && (bounds.right-bounds.left>1024 || bounds.bottom-bounds.top>768)) {
        SetWindowPos(hwnd,nullptr,0,0,1024,768,SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOMOVE);
    }
    file<<L"rect="<<bounds.left<<L","<<bounds.top<<L","<<bounds.right<<L","<<bounds.bottom<<L" ";
    file<<L"window pid="<<pid<<L" visible="<<IsWindowVisible(hwnd)<<L" title="<<title<<L"\n";
    return TRUE;
}
static BOOL CALLBACK close_owned_viewer(HWND hwnd, LPARAM) {
    DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE,FALSE,pid);
    if(p){
        wchar_t path[32768]{};DWORD size=32768;
        if(QueryFullProcessImageNameW(p,0,path,&size)){
            const wchar_t* name=wcsrchr(path,L'\\');
            if(name && _wcsicmp(name+1,L"WindowsSandboxRemoteSession.exe")==0)TerminateProcess(p,0);
        }
        CloseHandle(p);
    }
    return TRUE;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=5){std::cerr<<"Usage: viewer wsb-exe sandbox-id log-path stop-file\n";return 2;}
    const std::wstring name=L"BridgeBenchmark_"+std::to_wstring(GetCurrentProcessId());
    HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_ENUMERATE|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS|DESKTOP_SWITCHDESKTOP,nullptr);
    if(!desktop){std::cerr<<"CreateDesktop error "<<GetLastError();return 3;}
    std::wstring desktopPath=L"winsta0\\"+name;
    std::wstring command=L"\""+std::wstring(argv[1])+L"\" connect --id "+argv[2]+L" --raw";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.lpDesktop=desktopPath.data();
    startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(argv[1],command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){
        std::cerr<<"CreateProcess error "<<GetLastError();CloseDesktop(desktop);return 4;
    }
    CloseHandle(process.hThread);
    std::wofstream file(argv[3]);
    file<<L"desktop="<<desktopPath<<L" launcher_pid="<<process.dwProcessId<<L"\n";file.flush();
    // Bounded host diagnostic observation, not gameplay polling.
    for(int i=0;i<900;++i){
        if(GetFileAttributesW(argv[4])!=INVALID_FILE_ATTRIBUTES)break;
        file<<L"observation="<<i<<L"\n";
        EnumDesktopWindows(desktop,window_record,reinterpret_cast<LPARAM>(&file));file.flush();
        Sleep(1000);
    }
    CloseHandle(process.hProcess);
    std::wstring stop=L"\""+std::wstring(argv[1])+L"\" stop --id "+argv[2];
    PROCESS_INFORMATION cleanup{};
    if(CreateProcessW(argv[1],stop.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&cleanup)){
        WaitForSingleObject(cleanup.hProcess,15000);
        CloseHandle(cleanup.hThread);CloseHandle(cleanup.hProcess);
    }
    EnumDesktopWindows(desktop,close_owned_viewer,0);
    CloseDesktop(desktop);return 0;
}
