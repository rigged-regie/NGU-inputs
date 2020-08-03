#include <iostream>
#include <Windows.h>
#include <tlhelp32.h>
#include <thread>

namespace cfg {
	char const* default_dll_name = ".\\api.dll";
	char const* default_proc_name = "NGUIdle.exe";
}

template <typename ...Args>
void assert(bool cond, Args &&...args) {
	if (cond) return;

	std::cout << "assertion failed:\n\tmsg: ";
	((std::cout << std::forward<Args>(args) << " "), ...);
	std::cout << "\n\tcode: " << GetLastError() << "\n";
	std::cout << std::flush;
	getchar();
	exit(0);
}

DWORD get_proc_id_by_name(char const* name) {
	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (Process32First(snapshot, &entry))
		while (Process32Next(snapshot, &entry))
			if (!_stricmp(entry.szExeFile, name))
				return entry.th32ProcessID;

	return 0;
}

void inject_dll(DWORD proc_id, char const* dll_path) {
	HANDLE const hndl = OpenProcess(PROCESS_ALL_ACCESS, 0, proc_id);
	assert(hndl, "Unable to open target process");

	LPVOID const mem = VirtualAllocEx(hndl, NULL, strlen(dll_path) + 1, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	assert(mem, "Unable to allocate memory in target process");
	assert(WriteProcessMemory(hndl, mem, dll_path, strlen(dll_path) + 1, 0), "Unable to write into target process");

	LPVOID const loadLibraryAddress = (void*)GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryA");
	assert(loadLibraryAddress, "Unalbe to find LoadLibraryA address in target process");

	HANDLE thread = CreateRemoteThreadEx(hndl, 0, 0, (LPTHREAD_START_ROUTINE)loadLibraryAddress, mem, 0, 0, 0);
	assert(thread, "Unable to create remote thread, try with admin right?");
	assert(CloseHandle(hndl), "Unable to close target process handle");
}

int main(int argc, char **argv) {
	char const *target = (argc > 1 ? argv[1] : cfg::default_proc_name);
	char const *rel_dll_path = (argc > 2 ? argv[2] : cfg::default_dll_name);
	char abs_dll_path[1024];

	assert(_fullpath(abs_dll_path, rel_dll_path, 1024), "Unable to get absolute path");

	DWORD const proc_id = get_proc_id_by_name(target);
	assert(proc_id, "Unable to find proc id");

	inject_dll(proc_id, abs_dll_path);
	std::cout << "Injection successful" << std::endl;
	return 0;
}
