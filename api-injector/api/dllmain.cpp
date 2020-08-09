#include <type_traits>
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <Windows.h>

// #define DEBUG

#ifdef DEBUG
template <typename ...Args>
int debugf(char const* fmt, Args&& ...args) {
	return printf(fmt, std::forward<Args>(args)...);
}
#else
int debugf(char const*, ...) { return 0; }
#endif

namespace glb {
	HMODULE hmod;
	HWND hwnd;

	POINT screen_to_window(POINT p) {
		RECT rect;
		if (!GetWindowRect(glb::hwnd, &rect))
			debugf("|D| Unable to get game window rect %d\n", GetLastError());
		p.x += rect.left;
		p.y += rect.top;
		return p;
	}
}

namespace cfg {
	constexpr char const* cmd_pipe_name = "\\\\.\\pipe\\ngu_cmd";
}

namespace mem {
	bool read(char const* addr, char* buffer, size_t len, bool restore_prot = true) {
		DWORD oldprot;
		if (!VirtualProtect((LPVOID)addr, len, PAGE_EXECUTE_READWRITE, &oldprot))
			return false;
		memcpy(buffer, addr, len);
		if (restore_prot)
			VirtualProtect((LPVOID)addr, len, oldprot, &oldprot);
		return true;
	}

	bool write(char* addr, char const* buffer, size_t len, bool restore_prot = true) {
		DWORD oldprot;
		if (!VirtualProtect((LPVOID)addr, len, PAGE_EXECUTE_READWRITE, &oldprot))
			return false;
		memcpy(addr, buffer, len);
		if (restore_prot)
			VirtualProtect((LPVOID)addr, len, oldprot, &oldprot);
		return true;
	}

	// movabs r10, addr
	// jmp r10
	// takes 13 bytes
	bool write_abs_jmp64(char* addr, char* target) {
		char buffer[13];
		memcpy(buffer + 0, "\x49\xba", 2);
		memcpy(buffer + 2, (char*)&target, 8);
		memcpy(buffer + 10, "\x41\xFF\xe2", 3);
		return write(addr, buffer, 13);
	}

	// jmp rip + addr + 2
	// takes 2 bytes
	bool write_rel_jmp8(char* addr, char* target) {
		char buffer[2];
		memcpy(buffer + 0, "\xeb", 1);
		memcpy(buffer + 1, (char*)&target, 1);
		return write(addr, buffer, 2);
	}

	bool is_executable(char* addr) {
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)))         return false;
		if (mbi.State != MEM_COMMIT || mbi.Protect == PAGE_NOACCESS) return false;

		DWORD prot = mbi.Protect;
		return
			(prot == PAGE_EXECUTE ||
				prot == PAGE_EXECUTE_READ ||
				prot == PAGE_EXECUTE_READWRITE ||
				prot == PAGE_EXECUTE_WRITECOPY);
	}
}

namespace mono {
	namespace priv {
		void* thread;
		void* engine_img;
		void* engine_ui_img;

		void __cdecl assembly_enumerator(void* assembly, std::vector<void*>* assemblies) {
			assemblies->push_back(assembly);
		}
	}

	using GFunc = void(__cdecl*)(void* data, void* user_data);

	void(__cdecl* g_free)(void*);
	char* (__cdecl* mono_image_get_name)(void* image);
	int(__cdecl* mono_assembly_foreach)(GFunc func, void* user_data);
	void(__cdecl* mono_thread_detach)(void* monothread);
	void* (__cdecl* mono_assembly_get_image)(void* assembly);
	void* (__cdecl* mono_class_from_name)(void* image, char const* name_space, char const* name);
	void* (__cdecl* mono_class_get_method_from_name)(void* klass, char const* methodname, int paramcount);
	void* (__cdecl* mono_compile_method)(void* method);
	char* (__cdecl* mono_string_to_utf8)(void* str);
	void* (__cdecl* mono_thread_attach)(void* domain);
	void* (__cdecl* mono_get_root_domain)(void);

	bool init() {
		HMODULE mono = GetModuleHandleA("mono.dll");
		if (!mono) {
			printf("|E| Unable to find mono.dll\n");
			return false;
		}

		auto mono_func = [mono](auto& func, char const* name) -> bool {
			using typ = std::remove_reference_t<decltype(func)>;
			func = (typ)GetProcAddress(mono, name);
			if (!func) printf("|E| Unable to get address of %s\n", name);
			return func;
		};

		if (!mono_func(g_free, "g_free"))                          return false;
		if (!mono_func(mono_image_get_name, "mono_image_get_name"))             return false;
		if (!mono_func(mono_assembly_foreach, "mono_assembly_foreach"))           return false;
		if (!mono_func(mono_thread_detach, "mono_thread_detach"))              return false;
		if (!mono_func(mono_assembly_get_image, "mono_assembly_get_image"))         return false;
		if (!mono_func(mono_class_from_name, "mono_class_from_name"))            return false;
		if (!mono_func(mono_class_get_method_from_name, "mono_class_get_method_from_name")) return false;
		if (!mono_func(mono_compile_method, "mono_compile_method"))             return false;
		if (!mono_func(mono_string_to_utf8, "mono_string_to_utf8"))             return false;
		if (!mono_func(mono_thread_attach, "mono_thread_attach"))              return false;
		if (!mono_func(mono_get_root_domain, "mono_get_root_domain"))            return false;

		void* domain = mono_get_root_domain();
		if (!domain) {
			printf("|E| Unable to get mono root domain\n");
			return false;
		}

		priv::thread = mono_thread_attach(domain);
		if (!priv::thread) {
			printf("|E| Unable to attach mono thread\n");
			return false;
		}

		std::vector<void*> assemblies;
		mono_assembly_foreach((GFunc)priv::assembly_enumerator, &assemblies);

		debugf("|D| mono assemblies:\n");
		for (auto const& assembly : assemblies) {
			void *image = mono_assembly_get_image(assembly);
			char *image_name = mono_image_get_name(image);

			debugf("|D| \t %s\n", image_name);

			if (!strncmp(image_name, "UnityEngine", 12))
				priv::engine_img = image;
			if (!strncmp(image_name, "UnityEngine.UI", 15))
				priv::engine_ui_img = image;
		}

		if (!priv::engine_img) {
			printf("|E| Unable to find UnityEngine image\n");
			return false;
		}

		if (!priv::engine_ui_img) {
			printf("|E| Unable to find UnityEngine.UI image\n");
			return false;
		}

		return true;
	}

	void close() {
		mono_thread_detach(priv::thread);
	}

	enum class image : uint8_t {
		engine = 0,
		engine_ui = 1
	};

	void* get_method_addr(image img, char const* space, char const* class_name, char const* method_name, int params) {
		void* img_ptr;
		switch (img) {
		case image::engine:    img_ptr = priv::engine_img;    break;
		case image::engine_ui: img_ptr = priv::engine_ui_img; break;
		default: return 0;
		}

		void* klass = mono_class_from_name(img_ptr, space, class_name);
		if (!klass) return 0;

		void* method = mono_class_get_method_from_name(klass, method_name, params);
		if (!method) return 0;

		void* native = mono_compile_method(method);
		if (!native) return 0;

		return native;
	}
}

namespace addrs {
	char* on_app_focus;
	char* get_key_down;
	char* get_key;
	char* get_cur_pos;

	bool init() {
		get_cur_pos = (char*)::GetCursorPos;

		on_app_focus = (char*)mono::get_method_addr(mono::image::engine_ui, "UnityEngine.EventSystems", "EventSystem", "OnApplicationFocus", 1);
		get_key_down = (char*)mono::get_method_addr(mono::image::engine, "UnityEngine", "Input", "GetKeyDownInt", 1);
		get_key = (char*)mono::get_method_addr(mono::image::engine, "UnityEngine", "Input", "GetKeyString", 1);

		auto assert_found = [](char const* addr, char const* name) -> bool {
			if (!addr) printf("|E| Unable to find address of %s\n", name);
			else       printf("|I| found address of %s at %#08llx\n", name, (intptr_t)addr);
			return addr;
		};

		if (!assert_found(get_cur_pos, "get_cur_pos"))  return false;
		if (!assert_found(on_app_focus, "on_app_focus")) return false;
		if (!assert_found(get_key_down, "get_key_down")) return false;
		if (!assert_found(get_key, "get_key"))      return false;

		return true;
	}
}

namespace hk {
	namespace priv {
		char* buffer;
	}

	struct {
		bool  do_fake;
		int   fake;
		char* buffer;
	} get_key_down_dat;

	struct {
		bool        do_fake;
		std::string fake;
		char*       buffer;
	} get_key_dat;

	struct {
		bool  do_fake;
		POINT fake;
		char* buffer;
	} get_cur_pos_dat;

	int GetKeyDown(int code) {
		auto& dat = get_key_down_dat;

		if (dat.do_fake) {
			if (code == dat.fake) {
				dat.do_fake = false;
				return 1;
			}
			return 0;
		}

		using fn = int(*)(int);
		return fn(dat.buffer)(code);
	}

	int GetKey(void* addr) {
		auto& dat = get_key_dat;
		using fn = int(*)(void*);

		if (dat.do_fake) {
			char* str = mono::mono_string_to_utf8(addr);

			if (!str) {
				printf("|W| GetKey | Something went wrong\n");
			} else {
				bool const eq = (dat.fake == str);
				dat.do_fake = !eq;
				mono::g_free(str);
				return eq;
			}
		}

		return fn(dat.buffer)(addr);
	}

	BOOL WINAPI GetCursorPos(LPPOINT p) {
		auto& dat = get_cur_pos_dat;

		if (dat.do_fake) {
			memcpy(p, &dat.fake, sizeof(POINT));
			return TRUE;
		}

		using fn = BOOL(WINAPI*)(LPPOINT);
		return fn(dat.buffer)(p);
	}

	void on_app_focs(bool unhook = false) {
		if (unhook) {
			mem::write(addrs::on_app_focus + 32, "\x40\x88\x48\x48", 4);
		} else {
			mem::write(addrs::on_app_focus + 32, "\xC6\x40\x48\x00", 4);
		}
	}

	void get_key_down(bool unhook = false) {
		auto& dat = get_key_down_dat;

		if (unhook) {
			mem::write(addrs::get_key_down, "\x55\x48\x8b\xec", 4);
		} else {
			mem::write(dat.buffer, "\x55\x48\x8b\xec", 4);
			mem::write_abs_jmp64(dat.buffer + 4, addrs::get_key_down + 4);
			mem::write_abs_jmp64(addrs::get_key_down - 13, (char*)GetKeyDown);
			mem::write_rel_jmp8(addrs::get_key_down, (char*)-15);
		}
	}

	void get_key(bool unhook = false) {
		auto& dat = get_key_dat;

		if (unhook) {
			mem::write(addrs::get_key, "\x55\x48\x8b\xec", 4);
		} else {
			mem::write(dat.buffer, "\x55\x48\x8b\xec", 4);
			mem::write_abs_jmp64(dat.buffer + 4, addrs::get_key + 4);
			mem::write_abs_jmp64(addrs::get_key - 13, (char*)GetKey);
			mem::write_rel_jmp8(addrs::get_key, (char*)-15);
		}
	}

	void get_cur_pos(bool unhook = false) {
		auto& dat = get_cur_pos_dat;

		if (unhook) {
			mem::write(addrs::get_cur_pos, "\xBA\x01\x00\x00\x00", 5);
		} else {
			mem::write(dat.buffer, "\xBA\x01\x00\x00\x00", 5);
			mem::write_abs_jmp64(dat.buffer + 5, addrs::get_cur_pos + 5);
			mem::write_abs_jmp64(addrs::get_cur_pos - 13, (char*)GetCursorPos);
			mem::write_rel_jmp8(addrs::get_cur_pos, (char*)-15);
		}
	}

	bool init() {
		priv::buffer = (char*)VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
		if (!priv::buffer) return false;

		constexpr size_t step = 0x20;
		size_t off = 0;

		char** buffers[] = {
			&get_key_down_dat.buffer,
			&get_key_dat.buffer,
			&get_cur_pos_dat.buffer
		};

		for (char** buf : buffers) {
			*buf = priv::buffer + off;
			off += step;
		}

		return true;
	}
}

namespace cmd {
	void fake_cur_pos(BYTE const* args) {
		auto& dat = hk::get_cur_pos_dat;

		POINT p;
		p.x = *(short*)(args + 0);
		p.y = *(short*)(args + 2);
		p = glb::screen_to_window(p);

		memcpy(&dat.fake, &p, sizeof(POINT));
		dat.do_fake = true;
	}

	void restore_cur_pos(BYTE const* = NULL) {
		hk::get_cur_pos_dat.do_fake = false;
	}

	void fake_shortcut(BYTE const* args) {
		auto& dat = hk::get_key_down_dat;
		dat.fake = *(int*)(args + 0);
		dat.do_fake = true;
	}

	void restore_shortcut(BYTE const* = NULL) {
		hk::get_key_down_dat.do_fake = false;
	}

	void fake_special(BYTE const* args) {
		auto& dat = hk::get_key_dat;

		BYTE key = *(BYTE*)(args + 0);
		if (key == 0) dat.fake = "left shift";
		else if (key == 1) dat.fake = "right shift";
		else if (key == 2) dat.fake = "left control";
		else if (key == 3) dat.fake = "right control";
		else {
			printf("|W| Invalid argument passed into fake_special %hhu\n", key);
			return;
		}

		dat.do_fake = true;
	}

	void restore_special(BYTE const* = NULL) {
		hk::get_key_dat.do_fake = false;
	}

	void unhook(BYTE const* = NULL) {
		hk::on_app_focs(true);
		hk::get_cur_pos(true);
		hk::get_key_down(true);
		hk::get_key(true);
	}

	void eject(BYTE const* = NULL) {
		FreeConsole();
		FreeLibraryAndExitThread(glb::hmod, 0);
	}

	struct cmd {
		void(*const exec)(BYTE const* args);
		const DWORD args_len;
	};

	// useful for debugging which hook crashes the game ;p
	void hook_focus(BYTE const* = NULL) { hk::on_app_focs(); }
	void hook_get_cur_pos(BYTE const* = NULL) { hk::get_cur_pos(); }
	void hook_get_key_down(BYTE const* = NULL) { hk::get_key_down(); }
	void hook_get_key(BYTE const* = NULL) { hk::get_key(); }

	// take advantage of non-buffered communication
	// useful for syncing injected dll with pipe client
	void sync(BYTE const* = NULL) {}

	cmd const cmds[] = {
		{fake_cur_pos     , 4}, // 0x0 ( 0)
		{restore_cur_pos  , 0}, // 0x1 ( 1)
		{fake_shortcut    , 4}, // 0x2 ( 2)
		{restore_shortcut , 0}, // 0x3 ( 3)
		{fake_special     , 1}, // 0x4 ( 4)
		{restore_special  , 0}, // 0x5 ( 5)
		{unhook           , 0}, // 0x6 ( 6)
		{eject            , 0}, // 0x7 ( 7)
		{hook_focus       , 0}, // 0x8 ( 8)
		{hook_get_cur_pos , 0}, // 0x9 ( 9)
		{hook_get_key_down, 0}, // 0xa (10)
		{hook_get_key     , 0}, // 0xb (11)
		{sync             , 0}  // 0xc (12)
	};

	constexpr DWORD max_args_size = 4;
}

namespace com {
	namespace priv {
		BOOL read(HANDLE pipe, BYTE* buffer, DWORD len) {
			DWORD total = 0;
			DWORD bts;

			while (total < len) {
				if (!ReadFile(pipe, buffer + total, len - total, &bts, NULL)) {
					return FALSE;
				}

				total += bts;
			}

			return TRUE;
		}

		BOOL write(HANDLE pipe, BYTE const* buffer, DWORD len) {
			DWORD total = 0;
			DWORD bts;

			while (total < len) {
				if (!WriteFile(pipe, buffer + total, len - total, &bts, FALSE)) {
					return FALSE;
				}

				total += bts;
			}

			return TRUE;
		}
	}

	namespace cmd {
		HANDLE pipe = NULL;

		BOOL init() {
			SECURITY_DESCRIPTOR sd;
			InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
			SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);

			SECURITY_ATTRIBUTES sa = { 0 };
			sa.nLength = sizeof(SECURITY_ATTRIBUTES);
			sa.lpSecurityDescriptor = &sd;
			sa.bInheritHandle = FALSE;

			pipe = CreateNamedPipeA(
				cfg::cmd_pipe_name,
				PIPE_ACCESS_INBOUND,
				PIPE_TYPE_BYTE,
				PIPE_UNLIMITED_INSTANCES,
				0,
				0,
				0,
				NULL);

			return pipe != NULL && pipe != INVALID_HANDLE_VALUE;
		}

		BOOL wait_for_client() {
			return ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED;
		}

		BOOL read(BYTE* buffer, DWORD len) {
			return priv::read(pipe, buffer, len);
		}

		BOOL close() {
			BOOL status = TRUE;
			if (pipe) status = DisconnectNamedPipe(pipe);
			pipe = NULL;
			return status;
		}
	}
}

struct guard {
	guard() {
		if (!AllocConsole()) {
			HWND hwnd = FindWindowA(NULL, "NGU Idle");
			std::string msg = "Unable to allocate console " + std::to_string(GetLastError());
			MessageBoxA(hwnd, msg.c_str(), "oops", MB_OK);
		}

		freopen_s((FILE**)stdin, "CONIN$", "r", stdin);
		freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
	}

	~guard() {
		FreeConsole();
		mono::close();
	}
};

DWORD main_thread(LPVOID) {
	guard g;
	printf("|I| console initialzied\n");

	if (!mono::init()) { printf("|E| Unable to initialize mono\n");       return 0; }
	if (!addrs::init()) { printf("|E| Unable to initialize addresses\n"); return 0; }
	if (!hk::init()) { printf("|E| Unable to initialize hook buffers\n"); return 0; }

	BYTE cmd_typ;
	BYTE cmd_args[cmd::max_args_size];

	while (true) {
		if (!com::cmd::close()) printf("|E| Unable to close pipe %d\n", GetLastError());
		if (!com::cmd::init()) { printf("|E| Unable to initialize cmd pipe %d\n", GetLastError()); break; }
		if (!com::cmd::wait_for_client()) {
			printf("|W| Unable to connect to client %d\n", GetLastError());
			Sleep(100);
			continue;
		}

		while (true) {
			if (!com::cmd::read(&cmd_typ, 1)) {
				printf("|W| Unable to read command type\n");
				break;
			}

			if (cmd_typ >= sizeof(cmd::cmds) / sizeof(cmd::cmd)) {
				printf("|W| Command type is invalid\n");
				continue;
			}

			DWORD const args_len = cmd::cmds[cmd_typ].args_len;
			if (args_len && !com::cmd::read(cmd_args, args_len)) {
				printf("|W| Unable to read command arguments\n");
				break;
			}

			debugf("|D| cmd: %02x, args: ", cmd_typ);
			for (DWORD i = 0; i < args_len; ++i)
				debugf("%02x", cmd_args[i]);
			debugf("\n");

			cmd::cmds[cmd_typ].exec(cmd_args);
		}
	}

	return 0;
}

BOOL WINAPI DllMain(
	HINSTANCE hinstDLL,
	DWORD fdwReason,
	LPVOID lpReserved)
{
	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		glb::hmod = hinstDLL;
		glb::hwnd = FindWindow(NULL, "NGU Idle");

		if (!glb::hwnd) {
			MessageBoxA(NULL, "Unable to find game window", "oops", MB_OK);
			return TRUE;
		}
		CreateThread(NULL, 0, main_thread, NULL, 0, 0);
		break;

	case DLL_THREAD_ATTACH: break;
	case DLL_THREAD_DETACH: break;
	case DLL_PROCESS_DETACH: break;
	}
	return TRUE;
}
