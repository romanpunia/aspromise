#include "../src/aspromise.hpp"
#include <stdio.h>
#include <assert.h>
#include <string>
#include <sstream>
#include <thread>
#include <inttypes.h>
#include <queue>

/* How to execute this example */
enum class ExampleExecution
{
	NodeJSEventLoop_ExecutesNext,
	SettlementThread_ExecutesNext
};

/* Event loop callback temporary storage */
struct NextCallback
{
	AsBasicPromise<AsReactiveExecutor>* Promise;
	asIScriptFunction* Callback;
};

/* Context state globals */
static ExampleExecution ExecutionPolicy = ExampleExecution::NodeJSEventLoop_ExecutesNext;

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
	printf("\nset timeout for %" PRIu64 "ms (thread %s)\n", Ms, ThreadId.c_str());
}
void PrintResolveTimeout(uint64_t Ms)
{
	auto ThreadId = GetThreadId();
	printf("  triggered timer expiration (thread %s)\n", ThreadId.c_str());
}
void PrintResolveTimeoutAsync(uint32_t Id)
{
	auto ThreadId = GetThreadId();
	printf("  timer %i has been resolved through callback (thread %s)\n", Id, ThreadId.c_str());
}
void PrintAndWaitForInput(uint64_t Delta, uint32_t Switches)
{
	auto ThreadId = GetThreadId();
	printf("\ntest finished in %" PRIu64 "ms with %i context switches (thread %s)\n", Delta, Switches, ThreadId.c_str());
	(void)getchar();
}

/*
	Very simple crossplatform timer, i mean this is an example,
	i would not recommend spawning a thread each time timer is
	set. These are not precise btw.
*/
void SetTimeoutNative(uint64_t Ms, asIScriptFunction* Callback)
{
	asIScriptContext* ThisContext = asGetActiveContext();
	PROMISE_ASSERT(Callback != nullptr, "callback should not be null");
	PROMISE_ASSERT(ThisContext != nullptr, "timeout should be called within script environment");
	PrintSetTimeout(Ms);

	void* DelegateObject = Callback->GetDelegateObject();
	if (DelegateObject != nullptr)
		ThisContext->GetEngine()->AddRefScriptObject(DelegateObject, Callback->GetDelegateObjectType());

	std::thread([ThisContext, DelegateObject, Callback, Ms]()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(Ms));
		if (ExecutionPolicy == ExampleExecution::SettlementThread_ExecutesNext)
		{
			/*
				Callback will be executed in newly created context,
				i didn't find easier way to do that for this example,
				meaning fully multithreaded.
			*/
			asIScriptEngine* Engine = ThisContext->GetEngine();
			asIScriptContext* Context = Engine->RequestContext();
			PROMISE_ASSERT(Context != nullptr, "context creation is not possible");
			PROMISE_CHECK(Context->Prepare(Callback));
			int R = Context->Execute();
			PROMISE_ASSERT(R == asEXECUTION_FINISHED, "this example requires fully synchronous timer callback");

			/* Cleanup everything referenced */
			Engine->ReturnContext(Context);
            AsClearCallback(Callback);
		}
		else if (ExecutionPolicy == ExampleExecution::NodeJSEventLoop_ExecutesNext)
		{
			/* Callback will be executed in event loop */
			AsReactiveExecutor()(nullptr, ThisContext, Callback);
		}
		asThreadCleanup();
	}).detach();
}

/*
	Same as previous but promise will be returned, this is an example
	when promise is settled within C++
*/
void* SetTimeoutNativePromise(uint64_t Ms)
{
	void* Result = nullptr;
	if (ExecutionPolicy == ExampleExecution::SettlementThread_ExecutesNext)
		Result = AsDirectPromise::Create();
	else if (ExecutionPolicy == ExampleExecution::NodeJSEventLoop_ExecutesNext)
		Result = AsReactivePromise::Create();

	PrintSetTimeout(Ms);
	std::thread([Ms, Result]()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(Ms));
		PrintResolveTimeout(Ms); // Print as in script file

		int32_t Value = 1;
		if (ExecutionPolicy == ExampleExecution::SettlementThread_ExecutesNext)
		{
			AsDirectPromise* Promise = (AsDirectPromise*)Result;
			Promise->Store(&Value, asTYPEID_INT32); // Settle the promise
			/*
				Must release, returned promise ref-count is automatically incremented,
				in more complex environments additional logic may be required to maintain
				valid promise lifetime (!)
			*/
			Promise->Release();
		}
		else if (ExecutionPolicy == ExampleExecution::NodeJSEventLoop_ExecutesNext)
		{
			AsReactivePromise* Promise = (AsReactivePromise*)Result;
			Promise->Store(&Value, asTYPEID_INT32);
			Promise->Release();
		}
		asThreadCleanup();
	}).detach();

	return Result;
}

/* AngelScript to C++ promises */
void AwaitPromiseBlocking(void* Promise)
{
	if (ExecutionPolicy == ExampleExecution::SettlementThread_ExecutesNext)
	{
		/*
			Here we can block current thread because we know
			that some other thread will resolve the promise 
		*/
		int32_t Number = 0;
		((AsDirectPromise*)Promise)->WaitIf()->Retrieve(&Number, asTYPEID_INT32);
		printf("  received number %i from script context (blocking)\n", Number);
	}
	else if (ExecutionPolicy == ExampleExecution::NodeJSEventLoop_ExecutesNext)
	{
		asIScriptContext* Context = asGetActiveContext();
		PROMISE_ASSERT(Context != nullptr, "cannot access current context");
		Context->SetException("cannot block current thread to wait for promise");
		printf("  >> cannot do blocking await while using event loop <<\n");
	}
}
void AwaitPromiseNonBlocking(void* PromiseContext)
{
	auto* Context = asGetActiveContext();
	if (ExecutionPolicy == ExampleExecution::SettlementThread_ExecutesNext)
	{
		/* If promise is still pending the we suspend the context */
		AsDirectPromise* Promise = (AsDirectPromise*)PromiseContext;
		if (Context != nullptr && Promise->IsPending())
			PROMISE_CHECK(Context->Suspend());
		else
			Context = nullptr;

		Promise->When([Context](AsDirectPromise* Promise)
		{
			int32_t Number = 0;
			Promise->Retrieve(&Number, asTYPEID_INT32);
			printf("  received number %i from script context (non-blocking)\n", Number);
			/*
				If promise was pending then we resume the context otherwise this callback
				was called in-place
			*/
			if (Context != nullptr)
				PROMISE_CHECK(Context->Execute());
		});
	}
	else if (ExecutionPolicy == ExampleExecution::NodeJSEventLoop_ExecutesNext)
	{
		AsReactivePromise* Promise = (AsReactivePromise*)PromiseContext;
		if (Context != nullptr && Promise->IsPending())
			PROMISE_CHECK(Context->Suspend());
		else
			Context = nullptr;

		Promise->When([Context](AsReactivePromise* Promise)
		{
			int32_t Number = 0;
			Promise->Retrieve(&Number, asTYPEID_INT32);
			printf("  received number %i from script context (non-blocking)\n", Number);
			if (Context != nullptr)
				PROMISE_CHECK(Context->Execute());
		});
	}
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
	if (ExecutionPolicy == ExampleExecution::SettlementThread_ExecutesNext)
		AsDirectPromise::Register(Engine);
	else if (ExecutionPolicy == ExampleExecution::NodeJSEventLoop_ExecutesNext)
		AsReactivePromise::Register(Engine);
	PROMISE_CHECK(Engine->RegisterFuncdef("void timer_callback()"));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("uint64 get_milliseconds()", asFUNCTION(GetMilliseconds), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void print_resolve_timeout()", asFUNCTION(PrintResolveTimeout), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void print_resolve_timeout_async(uint32)", asFUNCTION(PrintResolveTimeoutAsync), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void print_and_wait_for_input(uint64, uint32)", asFUNCTION(PrintAndWaitForInput), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void set_timeout_native(uint64, timer_callback@)", asFUNCTION(SetTimeoutNative), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void await_promise_blocking(promise<int>@+)", asFUNCTION(AwaitPromiseBlocking), asCALL_CDECL));
	PROMISE_CHECK(Engine->RegisterGlobalFunction("void await_promise_non_blocking(promise<int>@+)", asFUNCTION(AwaitPromiseNonBlocking), asCALL_CDECL));
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
	char* Generated = AsGeneratePromiseEntrypoints(Code, &Size);
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
	asIScriptContext* Context = Engine->RequestContext();
	if (ExecutionPolicy == ExampleExecution::SettlementThread_ExecutesNext)
	{
		PROMISE_CHECK(Context->Prepare(Main));
		int R = Context->Execute();
		PROMISE_ASSERT(R == asEXECUTION_FINISHED || R == asEXECUTION_SUSPENDED, "check script code, it may have thrown an exception");
		while (IsAsyncContextBusy(Context))
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	else if (ExecutionPolicy == ExampleExecution::NodeJSEventLoop_ExecutesNext)
	{
		/* Event loop state */
		std::queue<NextCallback> Queue;
		std::condition_variable Condition;
		std::mutex Mutex;
		AsReactiveExecutor::ReactiveCallback Notify = [&Mutex, &Condition, &Queue](AsReactivePromise* Promise, asIScriptFunction* Callback)
		{
			std::unique_lock<std::mutex> Unique(Mutex);
			Queue.push({ Promise, Callback });
			Condition.notify_one();
		};

		/* Event loop setup */
		AsReactiveExecutor::SetCallback(Context, &Notify);

		/* Push main function onto the stack */
		Main->AddRef();
		Queue.push({ nullptr, Main });

		/* Event loop */
		while (IsAsyncContextBusy(Context) || !Queue.empty())
		{
			/* Block until we have something to execute */
			std::unique_lock<std::mutex> Unique(Mutex);
			Condition.wait_for(Unique, std::chrono::milliseconds(1000), [&Queue]() { return !Queue.empty(); });

			while (!Queue.empty())
			{
				/* Pop next callback from queue */
				NextCallback Next = std::move(Queue.front());
				Queue.pop();

				/* We request another context if main context cannot execute this function at the moment */
				asIScriptContext* ExecutingContext = Context;
				if (Next.Callback != nullptr && ExecutingContext->GetState() == asEXECUTION_SUSPENDED)
					ExecutingContext = Engine->RequestContext();
				
				/* Negate mutex while executing the callback */
				Unique.unlock();
				if (Next.Callback != nullptr)
				{
					PROMISE_CHECK(ExecutingContext->Prepare(Next.Callback));
					/* Ability to execute either a main function or a promise function */
					if (Next.Promise)
						PROMISE_CHECK(ExecutingContext->SetArgObject(0, Next.Promise));
				}
				int R = ExecutingContext->Execute();
				Unique.lock();

				/* Release associated state */
				if (Next.Callback != nullptr)
					AsClearCallback(Next.Callback);
				if (ExecutingContext != Context)
					Engine->ReturnContext(ExecutingContext);

				/* Check if we may continue execute callbacks */
				if (R == asEXECUTION_SUSPENDED)
				{
					/* Coroutines inside callbacks require a lot more complex logic */
					PROMISE_ASSERT(ExecutingContext == Context, "this event loop does not allow coroutine usage inside callbacks");
					break;
				}
				else if (R == asEXECUTION_FINISHED)
					continue;

				PROMISE_ASSERT(false, "check script code, it may have thrown an exception");
			}
		}
	}

	/* Clean up */
	Engine->ReturnContext(Context);
	Engine->ShutDownAndRelease();

	return 0;
}