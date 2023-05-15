#ifndef AS_PROMISE_HPP
#define AS_PROMISE_HPP
#ifndef PROMISE_CONFIG
#define PROMISE_CONFIG
#define PROMISE_TYPENAME "promise" // promise type
#define PROMISE_VOIDPOSTFIX "_v" // promise<void> type (promise_v)
#define PROMISE_WRAP "wrap" // promise setter function
#define PROMISE_UNWRAP "unwrap" // promise getter function
#define PROMISE_YIELD "yield" // promise awaiter function
#define PROMISE_WHEN "when" // promise callback function
#define PROMISE_EVENT "when_callback" // promise funcdef name
#define PROMISE_PENDING "pending" // promise status checker
#define PROMISE_AWAIT "co_await" // keyword for await (C++20 coroutines one love)
#define PROMISE_USERID 559 // promise user data identifier (any value)
#define PROMISE_NULLID -1 // empty promise type id
#define PROMISE_CALLBACKS true // allow <when> listener
#endif
#ifndef NDEBUG
#define PROMISE_ASSERT(Expression, Message) assert((Expression) && Message)
#define PROMISE_CHECK(Expression) (assert((Expression) >= 0))
#else
#define PROMISE_ASSERT(Expression, Message)
#define PROMISE_CHECK(Expression) (Expression)
#endif
#ifndef ANGELSCRIPT_H
#include <angelscript.h>
#endif
#include <assert.h>
#include <mutex>
#include <cctype>
#include <functional>

/*
	I use similar implementation only as a wrapper
	for C++ Promise<T, Executor> that has other
	functions like <then> and <when>, with some
	templates and preprocessor it could be easily
	converted to this script promise. I don't
	think here chaining is needed at all as
	script context is always a coroutine in
	it's essence and it won't block. For special
	cases callback based awaiting using <when>
	is supported.

	Functions like Promise.all (js) could be
	implemented in AngelScript i assume.

	see (native): https://github.com/romanpunia/mavi/blob/master/src/mavi/core/core.h?plain=1#L2887
	see (wrapper): https://github.com/romanpunia/mavi/blob/master/src/mavi/core/bindings.h?plain=1#L789
*/
template <typename Executor>
class AsPromise
{
private:
	/* Basically used from <any> class */
	struct Dynamic
	{
		union
		{
			asINT64 Integer;
			double Number;
			void* Object;
		};

		int TypeId = PROMISE_NULLID;
	};
#if PROMISE_CALLBACKS
	/* Callbacks storage */
	struct
	{
		std::function<void(AsPromise<Executor>*)> Native;
		asIScriptFunction* Wrapper = nullptr;
	} Callbacks;
#else
	std::condition_variable Ready;
#endif
private:
	asIScriptEngine* Engine;
	asIScriptContext* Context;
	std::atomic<int> RefCount;
	std::mutex Update;
	Dynamic Value;

public:
	/* Thread safe release */
	void Release()
	{
		RefCount &= 0x7FFFFFFF;
		if (--RefCount <= 0)
		{
			ReleaseReferences(nullptr);
			this->~AsPromise();
			asFreeMem((void*)this);
		}
	}
	/* Thread safe add reference */
	void AddRef()
	{
		RefCount = (RefCount & 0x7FFFFFFF) + 1;
	}
	/* For garbage collector to detect references */
	void EnumReferences(asIScriptEngine* OtherEngine)
	{
		if (Value.Object != nullptr && (Value.TypeId & asTYPEID_MASK_OBJECT))
		{
			asITypeInfo* SubType = Engine->GetTypeInfoById(Value.TypeId);
			if ((SubType->GetFlags() & asOBJ_REF))
				OtherEngine->GCEnumCallback(Value.Object);
			else if ((SubType->GetFlags() & asOBJ_VALUE) && (SubType->GetFlags() & asOBJ_GC))
				Engine->ForwardGCEnumReferences(Value.Object, SubType);

			asITypeInfo* Type = OtherEngine->GetTypeInfoById(Value.TypeId);
			if (Type != nullptr)
				OtherEngine->GCEnumCallback(Type);
		}
#if PROMISE_CALLBACKS
		if (Callbacks.Wrapper != nullptr)
			OtherEngine->GCEnumCallback(Callbacks.Wrapper);
#endif
	}
	/* For garbage collector to release references */
	void ReleaseReferences(asIScriptEngine*)
	{
		if (Value.TypeId & asTYPEID_MASK_OBJECT)
		{
			asITypeInfo* Type = Engine->GetTypeInfoById(Value.TypeId);
			Engine->ReleaseScriptObject(Value.Object, Type);
			if (Type != nullptr)
				Type->Release();
			Clean();
		}
#if PROMISE_CALLBACKS
		if (Callbacks.Wrapper != nullptr)
		{
			Callbacks.Wrapper->Release();
			Callbacks.Wrapper = nullptr;
		}
#endif
		if (Context != nullptr)
		{
			Context->Release();
			Context = nullptr;
		}
	}
	/* For garbage collector to mark */
	void SetFlag()
	{
		RefCount |= 0x80000000;
	}
	/* For garbage collector to check mark */
	bool GetFlag()
	{
		return (RefCount & 0x80000000) ? true : false;
	}
	/* For garbage collector to check reference count */
	int GetRefCount()
	{
		return (RefCount & 0x7FFFFFFF);
	}
	/* Receive stored type id of future value */
	int GetTypeIdOfObject()
	{
		return Value.TypeId;
	}
	/* Provide a native callback that should be fired when promise will be settled */
	void When(std::function<void(AsPromise<Executor>*)>&& NewCallback)
	{
#if PROMISE_CALLBACKS
		Callbacks.Native = std::move(NewCallback);
#else
		PROMISE_ASSERT(false, "native callback binder for <when> is not allowed");
#endif
	}
	/* Provide a script callback that should be fired when promise will be settled */
	void When(asIScriptFunction* NewCallback)
	{
#if PROMISE_CALLBACKS
		if (Callbacks.Wrapper != nullptr)
			Callbacks.Wrapper->Release();
		
		Callbacks.Wrapper = NewCallback;
		if (Callbacks.Wrapper != nullptr)
			Callbacks.Wrapper->AddRef();
#else
		PROMISE_ASSERT(false, "script callback binder for <when> is not allowed");
#endif
	}
	/*
		Thread safe store function, this is used as promise resolver function,
		will either only store the result or store result and execute callback
		that will resume suspended context and then release the promise (won't destroy)
	*/
	void Store(void* RefPointer, int RefTypeId)
	{
		Update.lock();
		PROMISE_ASSERT(Value.TypeId == PROMISE_NULLID, "promise should be settled only once");
		PROMISE_ASSERT(RefPointer != nullptr || RefTypeId == asTYPEID_VOID, "input pointer should not be null");
		PROMISE_ASSERT(Engine != nullptr, "promise is malformed (engine is null)");
		PROMISE_ASSERT(Context != nullptr, "promise is malformed (context is null)");

		if (Value.TypeId != PROMISE_NULLID)
		{
			asIScriptContext* ThisContext = asGetActiveContext();
			if (!ThisContext)
				ThisContext = Context;

			ThisContext->SetException("promise is already fulfilled");
			return Update.unlock();
		}

		if ((RefTypeId & asTYPEID_MASK_OBJECT))
		{
			asITypeInfo* Type = Engine->GetTypeInfoById(RefTypeId);
			if (Type != nullptr)
				Type->AddRef();
		}

		Value.TypeId = RefTypeId;
		if (Value.TypeId & asTYPEID_OBJHANDLE)
		{
			Value.Object = *(void**)RefPointer;
		}
		else if (Value.TypeId & asTYPEID_MASK_OBJECT)
		{
			Value.Object = Engine->CreateScriptObjectCopy(RefPointer, Engine->GetTypeInfoById(Value.TypeId));
		}
		else
		{
			Value.Integer = 0;
			int Size = Engine->GetSizeOfPrimitiveType(Value.TypeId);
			memcpy(&Value.Integer, RefPointer, Size);
		}

		bool SuspendOwned = Context->GetUserData(PROMISE_USERID) == (void*)this;
		if (SuspendOwned)
			Context->SetUserData(nullptr, PROMISE_USERID);

		bool WantsResume = (Context->GetState() != asEXECUTION_ACTIVE && SuspendOwned);
#if PROMISE_CALLBACKS
		Update.unlock();
		if (Callbacks.Native != nullptr)
		{
			Callbacks.Native(this);
			Callbacks.Native = nullptr;
		}

		if (Callbacks.Wrapper != nullptr)
		{
			AddRef();
			Executor()(this, Context, Callbacks.Wrapper);
			Callbacks.Wrapper->Release();
			Callbacks.Wrapper = nullptr;
		}
#else
		Ready.notify_all();
		Update.unlock();
#endif
		if (WantsResume)
			Executor()(this, Context);
		else if (SuspendOwned)
			Release();
	}
	/* Thread safe store function, a little easier for C++ usage */
	void Store(void* RefPointer, const char* TypeName)
	{
		PROMISE_ASSERT(Engine != nullptr, "promise is malformed (engine is null)");
		PROMISE_ASSERT(TypeName != nullptr, "typename should not be null");
		Store(RefPointer, Engine->GetTypeIdByDecl(TypeName));
	}
	/* Thread safe store function, for promise<void> */
	void StoreVoid()
	{
		Store(nullptr, asTYPEID_VOID);
	}
	/* Thread safe retrieve function, non-blocking try-retrieve future value */
	bool Retrieve(void* RefPointer, int RefTypeId)
	{
		PROMISE_ASSERT(Engine != nullptr, "promise is malformed (engine is null)");
		PROMISE_ASSERT(RefPointer != nullptr, "output pointer should not be null");
		if (Value.TypeId == PROMISE_NULLID)
			return false;

		if (RefTypeId & asTYPEID_OBJHANDLE)
		{
			if ((Value.TypeId & asTYPEID_MASK_OBJECT))
			{
				if ((Value.TypeId & asTYPEID_HANDLETOCONST) && !(RefTypeId & asTYPEID_HANDLETOCONST))
					return false;

				Engine->RefCastObject(Value.Object, Engine->GetTypeInfoById(Value.TypeId), Engine->GetTypeInfoById(RefTypeId), reinterpret_cast<void**>(RefPointer));
				if (*(asPWORD*)RefPointer == 0)
					return false;

				return true;
			}
		}
		else if (RefTypeId & asTYPEID_MASK_OBJECT)
		{
			if (Value.TypeId == RefTypeId)
			{
				Engine->AssignScriptObject(RefPointer, Value.Object, Engine->GetTypeInfoById(Value.TypeId));
				return true;
			}
		}
		else
		{
			int Size1 = Engine->GetSizeOfPrimitiveType(Value.TypeId);
			int Size2 = Engine->GetSizeOfPrimitiveType(RefTypeId);
			PROMISE_ASSERT(Size1 == Size2, "cannot map incompatible primitive types");

			if (Size1 == Size2)
			{
				memcpy(RefPointer, &Value.Integer, Size1);
				return true;
			}
		}

		return false;
	}
	/* Thread safe retrieve function, also non-blocking, another syntax is used */
	void* Retrieve()
	{
		if (Value.TypeId == PROMISE_NULLID)
			return nullptr;

		if (Value.TypeId & asTYPEID_OBJHANDLE)
			return &Value.Object;
		else if (Value.TypeId & asTYPEID_MASK_OBJECT)
			return Value.Object;
		else if (Value.TypeId <= asTYPEID_DOUBLE || Value.TypeId & asTYPEID_MASK_SEQNBR)
			return &Value.Integer;

		return nullptr;
	}
	/* Thread safe retrieve function, no-op */
	void RetrieveVoid()
	{
	}
	/* Can be used to check if promise is still pending */
	bool IsPending()
	{
		return Value.TypeId == PROMISE_NULLID;
	}
	/*
		This function should be called before retrieving the value
		from promise, it will either suspend current context and add
		reference to this promise if it is still pending or do nothing
		if promise was already settled
	*/
	AsPromise* YieldIf()
	{
		std::unique_lock<std::mutex> Unique(Update);
		if (Value.TypeId == PROMISE_NULLID && Context != nullptr)
		{
			AddRef();
			Context->SetUserData(this, PROMISE_USERID);
			Executor()(this);
			Context->Suspend();
		}

		return this;
	}
	/*
		This function can be used to await for promise
		within C++ code (blocking style)
	*/
	AsPromise* WaitIf()
	{
		if (!IsPending())
			return this;

		AddRef();
		{
			std::unique_lock<std::mutex> Unique(Update);
#if PROMISE_CALLBACKS
			if (IsPending())
			{
				std::condition_variable Ready;
				Callbacks.Native = [&Ready](AsPromise<Executor>*) { Ready.notify_all(); };
				Ready.wait(Unique, [this]() { return !IsPending(); });
			}
#else
			if (IsPending())
				Ready.wait(Unique, [this]() { return !IsPending(); });
#endif
		}
		Release();
		return this;
	}

private:
	/*
		Construct a promise, notify GC, set value to none,
		grab a reference to script context
	*/
	AsPromise(asIScriptContext* NewContext) noexcept : Engine(nullptr), Context(NewContext), RefCount(1)
	{
		PROMISE_ASSERT(Context != nullptr, "context should not be null");
		Context->AddRef();
		Engine = Context->GetEngine();
		Engine->NotifyGarbageCollectorOfNewObject(this, Engine->GetTypeInfoByName(PROMISE_TYPENAME));
		Clean();
	}
	/* Reset value to none */
	void Clean()
	{
		memset(&Value, 0, sizeof(Value));
		Value.TypeId = PROMISE_NULLID;
	}

public:
	/* AsPromise creation function, for use within C++ */
	static AsPromise* Create()
	{
		return new(asAllocMem(sizeof(AsPromise))) AsPromise(asGetActiveContext());
	}
	/* AsPromise creation function, for use within AngelScript */
	static AsPromise* CreateFactory(void* _Ref, int TypeId)
	{
		AsPromise* Future = new(asAllocMem(sizeof(AsPromise))) AsPromise(asGetActiveContext());
		if (TypeId != asTYPEID_VOID)
			Future->Store(_Ref, TypeId);

		return Future;
	}
	/* AsPromise creation function, for use within AngelScript (void promise) */
	static AsPromise* CreateFactoryVoid(void* _Ref, int TypeId)
	{
		return Create();
	}
	/* Template callback function for compiler, copy-paste from <array> class */
	static bool TemplateCallback(asITypeInfo* Info, bool& DontGarbageCollect)
	{
		int TypeId = Info->GetSubTypeId();
		if (TypeId == asTYPEID_VOID)
			return false;

		if ((TypeId & asTYPEID_MASK_OBJECT) && !(TypeId & asTYPEID_OBJHANDLE))
		{
			asIScriptEngine* Engine = Info->GetEngine();
			asITypeInfo* SubType = Engine->GetTypeInfoById(TypeId);
			asDWORD Flags = SubType->GetFlags();

			if ((Flags & asOBJ_VALUE) && !(Flags & asOBJ_POD))
			{
				bool Found = false;
				for (size_t i = 0; i < SubType->GetBehaviourCount(); i++)
				{
					asEBehaviours Behaviour;
					asIScriptFunction* Func = SubType->GetBehaviourByIndex((int)i, &Behaviour);
					if (Behaviour != asBEHAVE_CONSTRUCT)
						continue;

					if (Func->GetParamCount() == 0)
					{
						Found = true;
						break;
					}
				}

				if (!Found)
				{
					Engine->WriteMessage(PROMISE_TYPENAME, 0, 0, asMSGTYPE_ERROR, "The subtype has no default constructor");
					return false;
				}
			}
			else if ((Flags & asOBJ_REF))
			{
				bool Found = false;
				if (!Engine->GetEngineProperty(asEP_DISALLOW_VALUE_ASSIGN_FOR_REF_TYPE))
				{
					for (size_t i = 0; i < SubType->GetFactoryCount(); i++)
					{
						asIScriptFunction* Function = SubType->GetFactoryByIndex((int)i);
						if (Function->GetParamCount() == 0)
						{
							Found = true;
							break;
						}
					}
				}

				if (!Found)
				{
					Engine->WriteMessage(PROMISE_TYPENAME, 0, 0, asMSGTYPE_ERROR, "The subtype has no default factory");
					return false;
				}
			}

			if (!(Flags & asOBJ_GC))
				DontGarbageCollect = true;
		}
		else if (!(TypeId & asTYPEID_OBJHANDLE))
		{
			DontGarbageCollect = true;
		}
		else
		{
			asITypeInfo* SubType = Info->GetEngine()->GetTypeInfoById(TypeId);
			asDWORD Flags = SubType->GetFlags();

			if (!(Flags & asOBJ_GC))
			{
				if ((Flags & asOBJ_SCRIPT_OBJECT))
				{
					if ((Flags & asOBJ_NOINHERIT))
						DontGarbageCollect = true;
				}
				else
					DontGarbageCollect = true;
			}
		}

		return true;
	}
	/*
		Interface registration, note: promise<void> is not supported,
		instead use promise_v when internal datatype is not intended,
		promise will be an object handle with GC behaviours, default
		constructed promise will be pending otherwise early settled
	*/
	static void Register(asIScriptEngine* Engine)
	{
		using Type = AsPromise<Executor>;
		PROMISE_ASSERT(Engine != nullptr, "script engine should not be null");
		PROMISE_CHECK(Engine->RegisterObjectType(PROMISE_TYPENAME "<class T>", 0, asOBJ_REF | asOBJ_GC | asOBJ_TEMPLATE));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME "<T>", asBEHAVE_FACTORY, PROMISE_TYPENAME "<T>@ f(?&in)", asFUNCTION(Type::CreateFactory), asCALL_CDECL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME "<T>", asBEHAVE_TEMPLATE_CALLBACK, "bool f(int&in, bool&out)", asFUNCTION(Type::TemplateCallback), asCALL_CDECL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME "<T>", asBEHAVE_ADDREF, "void f()", asMETHOD(Type, AddRef), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME "<T>", asBEHAVE_RELEASE, "void f()", asMETHOD(Type, Release), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME "<T>", asBEHAVE_SETGCFLAG, "void f()", asMETHOD(Type, SetFlag), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME "<T>", asBEHAVE_GETGCFLAG, "bool f()", asMETHOD(Type, GetFlag), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME "<T>", asBEHAVE_GETREFCOUNT, "int f()", asMETHOD(Type, GetRefCount), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME "<T>", asBEHAVE_ENUMREFS, "void f(int&in)", asMETHOD(Type, EnumReferences), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME "<T>", asBEHAVE_RELEASEREFS, "void f(int&in)", asMETHOD(Type, ReleaseReferences), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectMethod(PROMISE_TYPENAME "<T>", "void " PROMISE_WRAP "(?&in)", asMETHODPR(Type, Store, (void*, int), void), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectMethod(PROMISE_TYPENAME "<T>", "T& " PROMISE_UNWRAP "()", asMETHODPR(Type, Retrieve, (), void*), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectMethod(PROMISE_TYPENAME "<T>", PROMISE_TYPENAME "<T>@+ " PROMISE_YIELD "()", asMETHOD(Type, YieldIf), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectMethod(PROMISE_TYPENAME "<T>", "bool " PROMISE_PENDING "()", asMETHOD(Type, IsPending), asCALL_THISCALL));
#if PROMISE_CALLBACKS
		PROMISE_CHECK(Engine->RegisterFuncdef("void " PROMISE_TYPENAME "<T>::" PROMISE_EVENT "(T&in)"));
		PROMISE_CHECK(Engine->RegisterObjectMethod(PROMISE_TYPENAME "<T>", "void " PROMISE_WHEN "(" PROMISE_EVENT "@+)", asMETHODPR(Type, When, (asIScriptFunction*), void), asCALL_THISCALL));
#endif
		PROMISE_CHECK(Engine->RegisterObjectType(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, 0, asOBJ_REF | asOBJ_GC));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, asBEHAVE_FACTORY, PROMISE_TYPENAME PROMISE_VOIDPOSTFIX "@ f()", asFUNCTION(Type::CreateFactoryVoid), asCALL_CDECL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, asBEHAVE_ADDREF, "void f()", asMETHOD(Type, AddRef), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, asBEHAVE_RELEASE, "void f()", asMETHOD(Type, Release), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, asBEHAVE_SETGCFLAG, "void f()", asMETHOD(Type, SetFlag), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, asBEHAVE_GETGCFLAG, "bool f()", asMETHOD(Type, GetFlag), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, asBEHAVE_GETREFCOUNT, "int f()", asMETHOD(Type, GetRefCount), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, asBEHAVE_ENUMREFS, "void f(int&in)", asMETHOD(Type, EnumReferences), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectBehaviour(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, asBEHAVE_RELEASEREFS, "void f(int&in)", asMETHOD(Type, ReleaseReferences), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectMethod(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, "void " PROMISE_WRAP "()", asMETHODPR(Type, StoreVoid, (), void), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectMethod(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, "void " PROMISE_UNWRAP "()", asMETHODPR(Type, RetrieveVoid, (), void), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectMethod(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, PROMISE_TYPENAME PROMISE_VOIDPOSTFIX "@+ " PROMISE_YIELD "()", asMETHOD(Type, YieldIf), asCALL_THISCALL));
		PROMISE_CHECK(Engine->RegisterObjectMethod(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, "bool " PROMISE_PENDING "()", asMETHOD(Type, IsPending), asCALL_THISCALL));
#if PROMISE_CALLBACKS
		PROMISE_CHECK(Engine->RegisterFuncdef("void " PROMISE_TYPENAME PROMISE_VOIDPOSTFIX "::" PROMISE_EVENT "()"));
		PROMISE_CHECK(Engine->RegisterObjectMethod(PROMISE_TYPENAME PROMISE_VOIDPOSTFIX, "void " PROMISE_WHEN "(" PROMISE_EVENT "@+)", asMETHODPR(Type, When, (asIScriptFunction*), void), asCALL_THISCALL));
#endif
	}
	/*
		Code generator function for custom syntax, in my in-engine implementation
		it uses std::basic_string and returns boolean, i decided not to include
		string to reduce #include bloating, it will allow JavaScript await syntax
		anywhere it could be used
	*/
	static char* GenerateEntrypoints(const char* Text, size_t Size)
	{
		PROMISE_ASSERT(Text != nullptr, "script code should not be null");
		const char Match[] = PROMISE_AWAIT " ";
		char* Code = (char*)asAllocMem(Size + 1);
		size_t MatchSize = sizeof(Match) - 1;
		size_t Offset = 0;
		memcpy(Code, Text, Size);
		Code[Size] = '\0';

		while (Offset < Size)
		{
			char U = Code[Offset];
			if (U == '/' && Offset + 1 < Size && Code[Offset + 1] == '/' || Code[Offset + 1] == '*')
			{
				if (Code[++Offset] == '*')
				{
					while (Offset + 1 < Size)
					{
						char N1 = Code[Offset++];
						char N2 = Code[Offset++];
						if (N1 == '*' && N2 == '/')
							break;
					}
				}
				else
				{
					while (Offset < Size)
					{
						char N = Code[Offset++];
						if (N == '\r' || N == '\n')
							break;
					}
				}

				continue;
			}
			else if (U == '\"' || U == '\'')
			{
				++Offset;
				while (Offset < Size)
				{
					if (Code[Offset++] == U)
						break;
				}

				continue;
			}
			else if (Size - Offset < MatchSize || memcmp(Code + Offset, Match, MatchSize) != 0)
			{
				++Offset;
				continue;
			}

			size_t Start = Offset + MatchSize;
			while (Start < Size)
			{
				char& V = Code[Start];
				if (!isspace(V))
					break;
				++Start;
			}

			int32_t Brackets = 0;
			size_t End = Start;
			while (End < Size)
			{
				char& V = Code[End];
				if (V == ')')
				{
					if (--Brackets < 0)
						break;
				}
				else if (V == '(')
					++Brackets;
				else if (V == ';')
					break;
				else if (Brackets == 0)
				{
					if (!isalnum(V) && V != '.' && V != ' ' && V != '_')
						break;
				}
				End++;
			}

			if (End - Start > 0)
			{
				const char Generator[] = "." PROMISE_YIELD "()." PROMISE_UNWRAP "()";
				char* Left = Code, *Middle = Code + Start, *Right = Code + End;
				size_t LeftSize = Offset;
				size_t MiddleSize = End - Start;
				size_t GeneratorSize = sizeof(Generator) - 1;
				size_t RightSize = Size - Offset;
				size_t SubstringSize = LeftSize + MiddleSize + GeneratorSize + RightSize;

				char* Substring = (char*)asAllocMem(SubstringSize + 1);
				memcpy(Substring, Left, LeftSize);
				memcpy(Substring + LeftSize, Middle, MiddleSize);
				memcpy(Substring + LeftSize + MiddleSize, Generator, GeneratorSize);
				memcpy(Substring + LeftSize + MiddleSize + GeneratorSize, Right, RightSize);
				Substring[SubstringSize] = '\0';
				asFreeMem(Code);

				Code = Substring;
				Size = strlen(Code);
				Offset += MiddleSize + GeneratorSize;
			}
			else
				Offset = End;
		}

		return Code;
	}
};

/*
	Basic promise settle executor, will
	resume context at thread that has
	settled the promise,
*/
struct SeqAsExecutor
{
	/* Called before suspend, this method will probably be optimized out */
	inline void operator()(AsPromise<SeqAsExecutor>* Promise)
	{
	}
	/* Called after suspend, this method will probably be inlined anyways */
	inline void operator()(AsPromise<SeqAsExecutor>* Promise, asIScriptContext* Context)
	{
		/*
			Context should be suspended at this moment but if for
			some reason it went active between function calls (multithreaded)
			then user is responsible for this task to be properly queued or
			exception should thrown if possible
		*/
		PROMISE_ASSERT(Context->GetState() == asEXECUTION_SUSPENDED, "context cannot be active while waiting for promise to settle");
		Context->Execute();

		/*
			YieldIf will add reference to context and promise,
			this will decrease reference count back to normal,
			otherwise memory will leak
		*/
		Promise->Release();
	}
	/* Called after suspend, for callback execution */
	inline void operator()(AsPromise<SeqAsExecutor>* Promise, asIScriptContext* ThisContext, asIScriptFunction* Callback)
	{
		asIScriptContext* Context = ThisContext->GetEngine()->CreateContext();
		PROMISE_ASSERT(Context != nullptr, "context creation is not possible");
		PROMISE_CHECK(Context->Prepare(Callback));
		PROMISE_CHECK(Context->SetArgAddress(0, Promise->Retrieve()));
		Context->Execute();
		Context->Release();
		Promise->Release();
	}
};

/* Typename exists for intended data type inside a promise */
template <typename T>
using SeqAsPromise = AsPromise<SeqAsExecutor>;
#endif