#include "promise.hpp"
#include <stdio.h>
#include <assert.h>
#include <string>
#include <thread>
#include <inttypes.h>

/* Current timestamp */
uint64_t GetMilliseconds()
{
	return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

/* Printing functions */
void PrintSetTimeout(uint64_t Ms)
{
	printf("set timeout for %" PRIu64 "ms\n", Ms);
}
void PrintResolveTimeout(uint64_t Ms)
{
	printf("triggered timer expiration\n");
}
void PrintAndWaitForInput(uint64_t Delta, uint32_t Switches)
{
	printf("test finished in %" PRIu64 "ms with %i context switches\n", Delta, Switches);
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
	PROMISE_CHECK(Engine->RegisterFuncdef("void timer_callback()"));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("uint64 get_milliseconds()", asFUNCTION(GetMilliseconds), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void print_set_timeout(uint64)", asFUNCTION(PrintSetTimeout), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void print_resolve_timeout()", asFUNCTION(PrintResolveTimeout), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void print_and_wait_for_input(uint64, uint32)", asFUNCTION(PrintAndWaitForInput), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void set_timeout_native(uint64, timer_callback@+)", asFUNCTION(SetTimeoutNative), asCALL_CDECL));
	SeqPromise::Register(Engine);

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

	/* Promise generators */
	char* Generated = SeqPromise::GenerateEntrypoints(Code, Size);
	asFreeMem(Code);
	Code = Generated;
	Size = strlen(Code);

	/* Module initialization */
	asIScriptModule* Module = Engine->GetModule(Path.c_str(), asGM_ALWAYS_CREATE);
	PROMISE_CHECK(Module->AddScriptSection(Path.c_str(), Code, Size));
	PROMISE_CHECK(Module->Build());

	/* Script entry point */
	asIScriptFunction* Main = Module->GetFunctionByDecl("void main()");
	assert(Main != nullptr);

	/* Context initialization */
	asIScriptContext* Context = Engine->CreateContext();
	PROMISE_CHECK(Context->Prepare(Main));

	/*
		Execution cycle, we execute only once here,
		then wait until there are now promises left,
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