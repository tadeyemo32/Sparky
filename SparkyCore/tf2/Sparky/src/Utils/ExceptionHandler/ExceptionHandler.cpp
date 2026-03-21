#include "ExceptionHandler.h"

#include "../../Features/Configs/Configs.h"

#include <ImageHlp.h>
#include <Psapi.h>
#include <deque>
#include <sstream>
#include <fstream>
#include <format>
#pragma comment(lib, "imagehlp.lib")

#define STATUS_RUNTIME_ERROR ((DWORD)0xE06D7363)

struct Frame_t
{
	std::string m_sModule = "";
	uintptr_t m_uBase = 0;
	uintptr_t m_uAddress = 0;
	std::string m_sFile = "";
	unsigned int m_uLine = 0;
	std::string m_sName = "";
};

static PVOID s_pHandle;
static LPVOID s_lpParam;
static std::unordered_map<LPVOID, bool> s_mAddresses = {};
static int s_iExceptions = 0;

static inline std::deque<Frame_t> StackTrace(PCONTEXT pContext)
{
	std::deque<Frame_t> vTrace = {};

	HANDLE hProcess = GetCurrentProcess();
	HANDLE hThread = GetCurrentThread();

	if (!SymInitialize(hProcess, nullptr, TRUE))
		return vTrace;

	SymSetOptions(SYMOPT_LOAD_LINES);

	STACKFRAME64 tStackFrame = {};
	tStackFrame.AddrPC.Offset = pContext->Rip;
	tStackFrame.AddrFrame.Offset = pContext->Rbp;
	tStackFrame.AddrStack.Offset = pContext->Rsp;
	tStackFrame.AddrPC.Mode = AddrModeFlat;
	tStackFrame.AddrFrame.Mode = AddrModeFlat;
	tStackFrame.AddrStack.Mode = AddrModeFlat;

	CONTEXT tContext = *pContext;

	while (StackWalk64(IMAGE_FILE_MACHINE_AMD64, hProcess, hThread, &tStackFrame, &tContext, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
	{
		vTrace.push_back({ .m_uAddress = tStackFrame.AddrPC.Offset });
		Frame_t& tFrame = vTrace.back();

		if (auto hBase = HINSTANCE(SymGetModuleBase64(hProcess, tStackFrame.AddrPC.Offset)))
		{
			tFrame.m_uBase = uintptr_t(hBase);

			char buffer[MAX_PATH];
			if (GetModuleBaseName(hProcess, hBase, buffer, sizeof(buffer) / sizeof(char)))
				tFrame.m_sModule = buffer;
			else
				tFrame.m_sModule = XSFMT(XS("{:#x}"), tFrame.m_uBase);
		}

		{
			DWORD dwOffset = 0;
			IMAGEHLP_LINE64 line = {};
			line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
			if (SymGetLineFromAddr64(hProcess, tStackFrame.AddrPC.Offset, &dwOffset, &line))
			{
				tFrame.m_sFile = line.FileName;
				tFrame.m_uLine = line.LineNumber;
				auto iFind = tFrame.m_sFile.rfind(XS("\\"));
				if (iFind != std::string::npos)
					tFrame.m_sFile.replace(0, iFind + 1, "");
			}
		}

		{
			uintptr_t dwOffset = 0;
			char buf[sizeof(IMAGEHLP_SYMBOL64) + 255];
			auto symbol = PIMAGEHLP_SYMBOL64(buf);
			symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64) + 255;
			symbol->MaxNameLength = 254;
			if (SymGetSymFromAddr64(hProcess, tStackFrame.AddrPC.Offset, &dwOffset, symbol))
				tFrame.m_sName = symbol->Name;
		}
	}

	SymCleanup(hProcess);

	return vTrace;
}

static LONG APIENTRY ExceptionFilter(PEXCEPTION_POINTERS ExceptionInfo)
{
	const char* sError = XS("UNKNOWN");
	switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
	case STATUS_ACCESS_VIOLATION: sError = XS("ACCESS VIOLATION"); break;
	case STATUS_STACK_OVERFLOW: sError = XS("STACK OVERFLOW"); break;
	case STATUS_HEAP_CORRUPTION: sError = XS("HEAP CORRUPTION"); break;
	case STATUS_RUNTIME_ERROR: sError = XS("RUNTIME ERROR"); break;
	case DBG_PRINTEXCEPTION_C: return EXCEPTION_EXECUTE_HANDLER;
	}

	if (s_mAddresses.contains(ExceptionInfo->ExceptionRecord->ExceptionAddress)
		|| !Vars::Debug::CrashLogging.Value
		|| s_iExceptions && GetAsyncKeyState(VK_SHIFT) & 0x8000 && GetAsyncKeyState(VK_RETURN) & 0x8000)
		return EXCEPTION_EXECUTE_HANDLER;
	s_mAddresses[ExceptionInfo->ExceptionRecord->ExceptionAddress];

	std::stringstream ssErrorStream;
	ssErrorStream << XSFMT(XS("Error: {} (0x{:X}) ({})\n"), sError, ExceptionInfo->ExceptionRecord->ExceptionCode, ++s_iExceptions);
	ssErrorStream << XS("Built @ ") __DATE__ XS(", ") __TIME__ XS(", ") __CONFIGURATION__ XS("\n");
	ssErrorStream << XSFMT(XS("Time @ {}, {}\n"), SDK::GetDate(), SDK::GetTime());

	ssErrorStream << XS("\n");
	if (U::Memory.GetOffsetFromBase(s_lpParam))
		ssErrorStream << XSFMT(XS("This: {}\n"), U::Memory.GetModuleOffset(s_lpParam));
	ssErrorStream << XSFMT(XS("RIP: {:#x}\n"), ExceptionInfo->ContextRecord->Rip);
	ssErrorStream << XSFMT(XS("RAX: {:#x}\n"), ExceptionInfo->ContextRecord->Rax);
	ssErrorStream << XSFMT(XS("RCX: {:#x}\n"), ExceptionInfo->ContextRecord->Rcx);
	ssErrorStream << XSFMT(XS("RDX: {:#x}\n"), ExceptionInfo->ContextRecord->Rdx);
	ssErrorStream << XSFMT(XS("RBX: {:#x}\n"), ExceptionInfo->ContextRecord->Rbx);
	ssErrorStream << XSFMT(XS("RSP: {:#x}\n"), ExceptionInfo->ContextRecord->Rsp);
	ssErrorStream << XSFMT(XS("RBP: {:#x}\n"), ExceptionInfo->ContextRecord->Rbp);
	ssErrorStream << XSFMT(XS("RSI: {:#x}\n"), ExceptionInfo->ContextRecord->Rsi);
	ssErrorStream << XSFMT(XS("RDI: {:#x}\n"), ExceptionInfo->ContextRecord->Rdi);

	ssErrorStream << XS("\n");
	if (auto vTrace = StackTrace(ExceptionInfo->ContextRecord);
		!vTrace.empty())
	{
		for (int i = 0; i < vTrace.size(); i++)
		{
			Frame_t& tFrame = vTrace[i];

			ssErrorStream << XSFMT(XS("{}: "), i + 1);
			if (tFrame.m_uBase)
				ssErrorStream << XSFMT(XS("{}+{:#x}"), tFrame.m_sModule, tFrame.m_uAddress - tFrame.m_uBase);
			else
				ssErrorStream << XSFMT(XS("{:#x}"), tFrame.m_uAddress);
			if (!tFrame.m_sFile.empty())
				ssErrorStream << XSFMT(XS(" ({} L{})"), tFrame.m_sFile, tFrame.m_uLine);
			if (!tFrame.m_sName.empty())
				ssErrorStream << XSFMT(XS(" ({})"), tFrame.m_sName);
			ssErrorStream << XS("\n");
		}
	}
	else
	{
		ssErrorStream << U::Memory.GetModuleOffset(ExceptionInfo->ExceptionRecord->ExceptionAddress);
		ssErrorStream << XS("\n");
	}

	try
	{
		std::ofstream file;
		file.open(F::Configs.m_sConfigPath + XS("crash_log.txt"), std::ios_base::app);
		file << ssErrorStream.str() + XS("\n\n\n");
		file.close();

		ssErrorStream << XS("\n");
		ssErrorStream << XS("Ctrl + C to copy. \n");
		ssErrorStream << XS("Logged to Sparky\\\\crash_log.txt. ");
	}
	catch (...) {}

	switch (ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
	case STATUS_ACCESS_VIOLATION:
	case STATUS_STACK_OVERFLOW:
	case STATUS_HEAP_CORRUPTION:
		SDK::Output(XS("Unhandled exception"), ssErrorStream.str().c_str(), {}, OUTPUT_DEBUG, MB_OK | MB_ICONERROR);
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

void CExceptionHandler::Initialize(LPVOID lpParam)
{
	s_pHandle = AddVectoredExceptionHandler(1, ExceptionFilter);
	s_lpParam = lpParam;
}
void CExceptionHandler::Unload()
{
	RemoveVectoredExceptionHandler(s_pHandle);
}