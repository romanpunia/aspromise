#include "promise.hpp"
#include <stdio.h>
#include <assert.h>
#include <string>
#include <sstream>
#include <thread>
#include <inttypes.h>

/* Thread utils */
std::string GetThreadId()
{
	std::stringstream Stream;
	Stream << std::this_thread::get_id();
	return Stream.str();
}

/* Current timestamp */
uint64_t GetMilliseconds()
{
	return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

/* Printing functions */
void PrintSetTimeout(uint64_t Ms)
{
	auto ThreadId = GetThreadId();
	printf("set timeout for %" PRIu64 "ms (thread %s)\n", Ms, ThreadId.c_str());
}
void PrintResolveTimeout(uint64_t Ms)
{
	auto ThreadId = GetThreadId();
	printf("triggered timer expiration (thread %s)\n", ThreadId.c_str());
}
void PrintResolveTimeoutAsync(uint32_t Id)
{
	auto ThreadId = GetThreadId();
	printf("timer %i has been resolved through callback (thread %s)\n", Id, ThreadId.c_str());
}
void PrintAndWaitForInput(uint64_t Delta, uint32_t Switches)
{
	auto ThreadId = GetThreadId();
	printf("test finished in %" PRIu64 "ms with %i context switches (thread %s)\n", Delta, Switches, ThreadId.c_str());
	(void)getchar();
}

/*
	Very simple crossplatform timer, i mean this is an example,
	i would not recommend spawning a thread each time timer is
	set. Also callback will be executed in newly created context,
	i didn't find easier way to do that for this example,
	meaning fully multithreaded. These are not precise btw.
*/
void SetTimeoutNative(uint64_t Ms, asIScriptFunction* Callback)
{
	asIScriptContext* ThisContext = asGetActiveContext();
	PROMISE_ASSERT(Callback != nullptr, "callback should not be null");
	PROMISE_ASSERT(ThisContext != nullptr, "timeout should be called within script environment");

	asIScriptContext* Context = ThisContext->GetEngine()->CreateContext();
	PROMISE_ASSERT(Context != nullptr, "context creation is not possible");
	PROMISE_CHECK(Context->Prepare(Callback));

	std::thread([Context, Ms]()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(Ms));
		int R = Context->Execute();
		PROMISE_ASSERT(R == asEXECUTION_FINISHED, "this example requires fully synchronous timer callback");
		Context->Release();
		asThreadCleanup();
	}).detach();
}

/*
	Same as previous but promise will be returned, this is an example
	when promise is settled within C++
*/
SeqPromise* SetTimeoutNativePromise(uint64_t Ms)
{
	SeqPromise* Result = SeqPromise::Create();
	std::thread([Ms, Result]()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(Ms));
		PrintResolveTimeout(Ms); // Print as in script file

		int32_t Value = 1;
		Result->Store(&Value, asTYPEID_INT32); // Settle the promise
	}).detach();

	return Result;
}

/* Compiler status logger */
void Log(const asSMessageInfo* Message, void*)
{
	static const char* Level[3] = { "err", "warn", "info" };
	printf("[%s] %s(%i,%i): %s\n", Level[(uint32_t)Message->type], Message->section, Message->row, Message->col, Message->message);
}

/* Entry point */
int main(int argc, char* argv[])
{
	/* Script path */
	std::string Path = argv[0];
	Path = Path.substr(0, Path.find_last_of("/\\") + 1) + "promises.as";

	/* Engine initialization */
	asIScriptEngine* Engine = asCreateScriptEngine();
	PROMISE_CHECK(Engine->SetMessageCallback(asFUNCTION(Log), 0, asCALL_CDECL));
	PROMISE_CHECK(Engine->SetEngineProperty(asEP_USE_CHARACTER_LITERALS, 1));
	
	/* Interface registration */
	SeqPromise::Register(Engine);
	PROMISE_CHECK(Engine->RegisterFuncdef("void timer_callback()"));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("uint64 get_milliseconds()", asFUNCTION(GetMilliseconds), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void print_set_timeout(uint64)", asFUNCTION(PrintSetTimeout), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void print_resolve_timeout()", asFUNCTION(PrintResolveTimeout), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void print_resolve_timeout_async(uint32)", asFUNCTION(PrintResolveTimeoutAsync), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void print_and_wait_for_input(uint64, uint32)", asFUNCTION(PrintAndWaitForInput), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void set_timeout_native(uint64, timer_callback@+)", asFUNCTION(SetTimeoutNative), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("promise<int32>@+ set_timeout_native_promise(uint64)", asFUNCTION(SetTimeoutNativePromise), asCALL_CDECL));

	/* Script dump */
	FILE* Stream = (FILE*)fopen(Path.c_str(), "rb");
	assert(Stream != nullptr);
	fseek(Stream, 0, SEEK_END);
	size_t Size = ftell(Stream);
	fseek(Stream, 0, SEEK_SET);

	char* Code = (char*)asAllocMem(sizeof(char) * (Size + 1));
	fread((char*)Code, sizeof(char), Size, Stream);
	Code[Size] = '\0';
	fclose(Stream);

	/* Promise syntax preprocessing */
	char* Generated = SeqPromise::GenerateEntrypoints(Code, Size);
	Size = strlen(Generated);
	asFreeMem(Code);

	/* Module initialization */
	asIScriptModule* Module = Engine->GetModule(Path.c_str(), asGM_ALWAYS_CREATE);
	PROMISE_CHECK(Module->AddScriptSection(Path.c_str(), Generated, Size));
	PROMISE_CHECK(Module->Build());
	asFreeMem(Generated);

	/* Script entry point */
	asIScriptFunction* Main = Module->GetFunctionByDecl("void main()");
	assert(Main != nullptr);

	/* Context initialization */
	asIScriptContext* Context = Engine->CreateContext();
	PROMISE_CHECK(Context->Prepare(Main));

	/*
		Execution cycle, we execute only once here,
		then wait until there are no promises left,
		it could be implemented differently through
		conditional variables, thread pool, event loop or
		simple execute cycle using appropriate executor.
	*/
	int R = Context->Execute();
	PROMISE_ASSERT(R == asEXECUTION_FINISHED || R == asEXECUTION_SUSPENDED, "check script code, it may have thrown an exception");
	while (Context->GetUserData(PROMISE_USERID) != nullptr || Context->GetState() != asEXECUTION_FINISHED)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

	/* Clean up */
	Context->Release();
	Engine->ShutDownAndRelease();
	asThreadCleanup();

	return 0;
}