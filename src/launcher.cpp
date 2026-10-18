#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include "kvr_version.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace {
namespace fs = std::filesystem;
constexpr wchar_t kExpectedSha256[] =
    L"9c10e0450a6eeca417e036e3cde7474fed1f0a92aab018446d156944dea91435";
constexpr DWORD kThreadTimeoutMs = 30000;

struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const { return value && value != INVALID_HANDLE_VALUE; }
};

struct Failure : std::exception {
    std::wstring message;
    explicit Failure(std::wstring text) : message(std::move(text)) {}
    const char* what() const noexcept override { return "KOTOR VR launcher failed"; }
};

std::wstring WinError(const wchar_t* operation, DWORD code = GetLastError()) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = std::wstring(operation) + L" (Windows error " +
        std::to_wstring(code) + L")";
    if (length && buffer) text += L": " + std::wstring(buffer, length);
    if (buffer) LocalFree(buffer);
    return text;
}

void Require(bool condition, const wchar_t* operation) {
    if (!condition) throw Failure(WinError(operation));
}

void CheckNt(NTSTATUS status, const wchar_t* operation) {
    if (status < 0) {
        std::wostringstream text;
        text << operation << L" (NTSTATUS 0x" << std::hex
             << static_cast<unsigned long>(status) << L")";
        throw Failure(text.str());
    }
}

fs::path ModulePath(HMODULE module) {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(module, buffer.data(),
                                               static_cast<DWORD>(buffer.size()));
        Require(length != 0, L"GetModuleFileNameW");
        if (length < buffer.size()) return fs::path(std::wstring(buffer.data(), length));
        if (buffer.size() >= 32768) throw Failure(L"Module path exceeds Windows path limit.");
        buffer.resize(buffer.size() * 2);
    }
}

fs::path AbsolutePath(const fs::path& path) {
    std::error_code error;
    fs::path result = fs::weakly_canonical(fs::absolute(path), error);
    if (error) throw Failure(L"Cannot resolve path: " + path.wstring());
    return result;
}

void RequireFile(const fs::path& path) {
    std::error_code error;
    if (!fs::is_regular_file(path, error))
        throw Failure(L"Required file does not exist: " + path.wstring());
}

std::wstring Sha256(const fs::path& path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    Require(file.valid(), L"Open swkotor.exe for SHA-256 verification");
    struct HashContext {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<UCHAR> object;
        ~HashContext() {
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } context;
    CheckNt(BCryptOpenAlgorithmProvider(&context.algorithm, BCRYPT_SHA256_ALGORITHM,
                                        nullptr, 0), L"BCryptOpenAlgorithmProvider");
    DWORD objectLength = 0, bytesReturned = 0;
    CheckNt(BCryptGetProperty(context.algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &bytesReturned, 0),
        L"BCryptGetProperty");
    context.object.resize(objectLength);
    CheckNt(BCryptCreateHash(context.algorithm, &context.hash, context.object.data(),
        objectLength, nullptr, 0, 0), L"BCryptCreateHash");
    std::array<UCHAR, 65536> buffer{};
    for (;;) {
        DWORD read = 0;
        Require(ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()),
                         &read, nullptr) != FALSE, L"Read swkotor.exe");
        if (!read) break;
        CheckNt(BCryptHashData(context.hash, buffer.data(), read, 0), L"BCryptHashData");
    }
    std::array<UCHAR, 32> digest{};
    CheckNt(BCryptFinishHash(context.hash, digest.data(),
                             static_cast<ULONG>(digest.size()), 0), L"BCryptFinishHash");
    std::wostringstream text;
    text << std::hex << std::setfill(L'0');
    for (UCHAR byte : digest) text << std::setw(2) << static_cast<unsigned>(byte);
    return text.str();
}

// Quote one argument using the CommandLineToArgvW / CRT backslash rules.
std::wstring Quote(const std::wstring& argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') { ++slashes; continue; }
        if (character == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result += character;
        } else {
            result.append(slashes, L'\\');
            result += character;
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}

// Toolhelp can return ERROR_PARTIAL_COPY before the suspended child's loader
// creates its module list. Mapped image regions already exist at this point.
// Query their actual file names and bases rather than assuming matching ASLR.
std::uintptr_t RemoteMappedImage(HANDLE process, const std::wstring& basename) {
    using MappedNameFunction = DWORD(WINAPI*)(HANDLE, LPVOID, LPWSTR, DWORD);
    const auto mappedName = reinterpret_cast<MappedNameFunction>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetMappedFileNameW"));
    Require(mappedName != nullptr, L"Resolve K32GetMappedFileNameW");
    std::uintptr_t address = 0;
    std::vector<wchar_t> name(32768);
    for (;;) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address),
                            &region, sizeof(region))) {
            if (GetLastError() == ERROR_INVALID_PARAMETER) break;
            throw Failure(WinError(L"Query suspended process image regions"));
        }
        if (region.Type == MEM_IMAGE && region.BaseAddress == region.AllocationBase) {
            const DWORD length = mappedName(process, region.AllocationBase,
                name.data(), static_cast<DWORD>(name.size()));
            if (length) {
                const fs::path image(std::wstring(name.data(), length));
                if (_wcsicmp(image.filename().c_str(), basename.c_str()) == 0)
                    return reinterpret_cast<std::uintptr_t>(region.AllocationBase);
            }
        }
        const auto regionBase = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        const auto nextAddress = regionBase + region.RegionSize;
        if (nextAddress <= address) break;
        address = nextAddress;
    }
    return 0;
}

std::uintptr_t RemoteModule(HANDLE process, DWORD processId,
                            const std::wstring& basename,
                            const fs::path* fullPath = nullptr) {
    HANDLE rawSnapshot = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
        rawSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE |
                                               TH32CS_SNAPMODULE32, processId);
        if (rawSnapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
        Sleep(10);
    }
    Handle snapshot(rawSnapshot);
    if (!snapshot.valid() && GetLastError() == ERROR_PARTIAL_COPY && !fullPath)
        return RemoteMappedImage(process, basename);
    Require(snapshot.valid(), L"CreateToolhelp32Snapshot");
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot.value, &entry)) {
        if (GetLastError() == ERROR_PARTIAL_COPY && !fullPath)
            return RemoteMappedImage(process, basename);
        if (GetLastError() == ERROR_NO_MORE_FILES) return 0;
        throw Failure(WinError(L"Module32FirstW"));
    }
    do {
        if (_wcsicmp(entry.szModule, basename.c_str()) != 0) continue;
        if (fullPath && _wcsicmp(AbsolutePath(entry.szExePath).c_str(), fullPath->c_str()) != 0)
            continue;
        return reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
    } while (Module32NextW(snapshot.value, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) throw Failure(WinError(L"Module32NextW"));
    return 0;
}

struct RemoteFunction {
    std::wstring module;
    std::uintptr_t rva;
};

RemoteFunction FunctionLocation(HMODULE module, const char* name) {
    FARPROC function = GetProcAddress(module, name);
    Require(function != nullptr, L"GetProcAddress");
    HMODULE owner = nullptr;
    Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(function), &owner) != FALSE, L"Find exported function owner");
    return { ModulePath(owner).filename().wstring(),
        reinterpret_cast<std::uintptr_t>(function) - reinterpret_cast<std::uintptr_t>(owner) };
}

// Owns only the process created here. Until ResumeThread succeeds, any failure
// terminates it, including hung injection threads, before remote memory is released.
struct ChildProcess {
    PROCESS_INFORMATION info{};
    bool resumed = false;
    ~ChildProcess() {
        if (info.hProcess && !resumed) {
            TerminateProcess(info.hProcess, 1);
            WaitForSingleObject(info.hProcess, 5000);
        }
        if (info.hThread) CloseHandle(info.hThread);
        if (info.hProcess) CloseHandle(info.hProcess);
    }
    void WaitThread(HANDLE thread, const wchar_t* operation) {
        const DWORD result = WaitForSingleObject(thread, kThreadTimeoutMs);
        if (result == WAIT_OBJECT_0) return;
        const DWORD error = GetLastError();
        TerminateProcess(info.hProcess, 1);
        WaitForSingleObject(info.hProcess, 5000);
        if (result == WAIT_TIMEOUT)
            throw Failure(std::wstring(operation) + L" timed out after 30 seconds; child terminated.");
        throw Failure(WinError(operation, error));
    }
};

void InitializeLoaderIfNeeded(ChildProcess& child, const RemoteFunction& loadLibrary) {
    if (RemoteModule(child.info.hProcess, child.info.dwProcessId, loadLibrary.module)) return;
    // ntdll is mapped before the primary thread starts. A newly created remote
    // thread runs Windows' process-loader initialization and then exits, leaving
    // the game's primary thread suspended and its entry point untouched.
    const RemoteFunction exitThread = FunctionLocation(GetModuleHandleW(L"ntdll.dll"),
                                                       "RtlExitUserThread");
    const auto remoteNtdll = RemoteModule(child.info.hProcess, child.info.dwProcessId,
                                          exitThread.module);
    if (!remoteNtdll) throw Failure(L"Cannot locate remote ntdll for loader initialization.");
    Handle thread(CreateRemoteThread(child.info.hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteNtdll + exitThread.rva),
        nullptr, 0, nullptr));
    Require(thread.valid(), L"Initialize suspended process loader");
    child.WaitThread(thread.value, L"Initialize suspended process loader");
}

void Inject(ChildProcess& child, const fs::path& dll) {
    const RemoteFunction loadLibrary = FunctionLocation(GetModuleHandleW(L"kernel32.dll"),
                                                         "LoadLibraryW");
    InitializeLoaderIfNeeded(child, loadLibrary);
    const auto remoteOwner = RemoteModule(child.info.hProcess, child.info.dwProcessId,
                                           loadLibrary.module);
    if (!remoteOwner) throw Failure(L"LoadLibraryW owner DLL is absent from the child process: " +
                                    loadLibrary.module);
    const std::wstring dllText = dll.wstring();
    const SIZE_T byteCount = (dllText.size() + 1) * sizeof(wchar_t);
    struct RemoteAllocation {
        HANDLE process;
        void* address;
        ~RemoteAllocation() { if (address) VirtualFreeEx(process, address, 0, MEM_RELEASE); }
    } memory{ child.info.hProcess, VirtualAllocEx(child.info.hProcess, nullptr,
                byteCount, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE) };
    Require(memory.address != nullptr, L"VirtualAllocEx");
    SIZE_T written = 0;
    Require(WriteProcessMemory(child.info.hProcess, memory.address, dllText.c_str(),
                              byteCount, &written) != FALSE, L"Write DLL path into child");
    if (written != byteCount) throw Failure(L"DLL path was only partially written into child.");
    Handle thread(CreateRemoteThread(child.info.hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteOwner + loadLibrary.rva),
        memory.address, 0, nullptr));
    Require(thread.valid(), L"Create LoadLibraryW injection thread");
    child.WaitThread(thread.value, L"LoadLibraryW injection");
    DWORD result = 0;
    Require(GetExitCodeThread(thread.value, &result) != FALSE, L"Read LoadLibraryW result");
    if (!result) throw Failure(L"Remote LoadLibraryW failed. Check DLL dependencies and bitness.");
    if (!RemoteModule(child.info.hProcess, child.info.dwProcessId,
                      dll.filename().wstring(), &dll))
        throw Failure(L"Injected DLL was not found at the expected full path in the child.");
}

void SelectRuntime(const fs::path& requested) {
    if (!requested.empty()) {
        const fs::path runtime = AbsolutePath(requested);
        RequireFile(runtime);
        Require(SetEnvironmentVariableW(L"XR_RUNTIME_JSON", runtime.c_str()) != FALSE,
                L"Set child OpenXR runtime");
        std::wcout << L"OpenXR runtime: " << runtime.wstring() << L"\n";
        return;
    }
    if (GetEnvironmentVariableW(L"XR_RUNTIME_JSON", nullptr, 0)) {
        std::wcout << L"Using inherited XR_RUNTIME_JSON.\n";
        return;
    }
    const fs::path vdxr = L"C:\\Program Files\\Virtual Desktop Streamer\\OpenXR\\virtualdesktop-openxr-32.json";
    std::error_code error;
    if (fs::is_regular_file(vdxr, error)) {
        Require(SetEnvironmentVariableW(L"XR_RUNTIME_JSON", vdxr.c_str()) != FALSE,
                L"Select Virtual Desktop 32-bit OpenXR runtime");
        std::wcout << L"OpenXR runtime: " << vdxr.wstring() << L"\n";
    } else {
        std::wcout << L"Using system OpenXR runtime (no VDXR32 override found).\n";
    }
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    static_assert(sizeof(void*) == 4, "Build the launcher for Win32/x86, matching swkotor.exe.");
    try {
        const fs::path launcher = AbsolutePath(ModulePath(nullptr));
        fs::path gameDirectory = launcher.parent_path().parent_path().parent_path();
        fs::path runtime;
        std::vector<std::wstring> gameArguments;
        for (int i = 1; i < argc; ++i) {
            const std::wstring argument = argv[i];
            if (argument == L"--version") { std::cout << KVR_VERSION << "\n"; return 0; }
            if (argument == L"--help" || argument == L"-h") {
                std::wcout << L"Usage: kotor-vr-launcher.exe [--game-dir DIR] [--xr-runtime JSON] [-- GAME_ARGS...]\n"
                    L"The launcher requires the supported unmodified GOG 1.03 swkotor.exe and adjacent kotor_vr.dll.\n";
                return 0;
            }
            if (argument == L"--") {
                while (++i < argc) gameArguments.emplace_back(argv[i]);
                break;
            }
            if (argument == L"--game-dir" || argument == L"--xr-runtime") {
                if (++i >= argc) throw Failure(L"Missing value for " + argument);
                if (argument == L"--game-dir") gameDirectory = argv[i];
                else runtime = argv[i];
            } else {
                throw Failure(L"Unknown argument: " + argument + L". Use --help for usage.");
            }
        }
        gameDirectory = AbsolutePath(gameDirectory);
        const fs::path executable = gameDirectory / L"swkotor.exe";
        const fs::path dll = AbsolutePath(launcher.parent_path() / L"kotor_vr.dll");
        RequireFile(executable);
        RequireFile(dll);
        const std::wstring digest = Sha256(executable);
        if (digest != kExpectedSha256)
            throw Failure(L"Unsupported swkotor.exe. No game was launched.\nExpected SHA-256: " +
                          std::wstring(kExpectedSha256) + L"\nActual SHA-256: " + digest);
        std::wcout << L"Verified supported swkotor.exe SHA-256.\n";
        SelectRuntime(runtime);
        std::wstring command = Quote(executable.wstring());
        for (const auto& argument : gameArguments) command += L" " + Quote(argument);
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        ChildProcess child;
        Require(CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr,
            FALSE, CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr,
            gameDirectory.c_str(), &startup, &child.info) != FALSE, L"Create suspended swkotor.exe");
        Inject(child, dll);
        Require(ResumeThread(child.info.hThread) != static_cast<DWORD>(-1), L"Resume game");
        child.resumed = true;
        std::wcout << L"VR DLL verified in process " << child.info.dwProcessId
                   << L". Game resumed. The original EXE and INI were not modified.\n";
        return 0;
    } catch (const Failure& failure) {
        std::wcerr << failure.message << L"\n";
        MessageBoxW(nullptr, failure.message.c_str(), L"KOTOR VR launcher", MB_OK | MB_ICONERROR);
    } catch (const std::exception& error) {
        const std::string message = error.what();
        const std::wstring wide(message.begin(), message.end());
        std::wcerr << wide << L"\n";
        MessageBoxW(nullptr, wide.c_str(), L"KOTOR VR launcher", MB_OK | MB_ICONERROR);
    }
    return 1;
}
