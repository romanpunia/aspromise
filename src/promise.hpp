#ifndef AS_PROMISE_HPP
#define AS_PROMISE_HPP
#define PROMISE_TYPENAME "promise" // promise type
#define PROMISE_WRAP "wrap" // promise setter function
#define PROMISE_UNWRAP "unwrap" // promise getter function
#define PROMISE_YIELD "yield" // promise awaiter function
#define PROMISE_PENDING "pending" // promise status checker
#define PROMISE_AWAIT "co_await" // keyword for await (C++20 coroutines one love)
#define PROMISE_USERID 559
#ifndef NDEBUG
#define PROMISE_ASSERT(Expression, Message) assert((Expression) && Message)
#define PROMISE_CHECK(Expression) (assert((Expression) >= 0))
#else
#define PROMISE_ASSERT(Expression, Message)
#define PROMISE_CHECK(Expression) (Expression)
#endif
#include <angelscript.h>
#include <assert.h>
#include <mutex>
#include <cctype>

/*
	I use similar implementation only as a wrapper
	for C++ Promise<T, Executor> that has other
	functions like <then> and <when>, with some
	templates and preprocessor it could be easily
	converted to this script promise. I don't
	think here chaining is needed at all as
	script context is always a coroutine in
	it's essence and it won't block.

	Functions like Promise.all (js) could be
	implemented in AngelScript i assume.

	see (native): https://github.com/romanpunia/edge/blob/master/src/edge/core/core.h?plain=1#L2887
	see (wrapper): https://github.com/romanpunia/edge/blob/master/src/edge/core/bindings.h?plain=1#L789
*/
template <typename Executor>
class BasicPromise
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

		int TypeId;
	};

private:
	asIScriptEngine* Engine;
	asIScriptContext* Context;
	std::mutex Update;
	Dynamic Value;
	int RefCount;
	bool Marked;

public:
	/* Thread safe release */
	void Release()
	{
		Marked = false;
		if (asAtomicDec(RefCount) <= 0)
		{
			ReleaseReferences(nullptr);
			this->~BasicPromise();
			asFreeMem((void*)this);
		}
	}
	/* Thread safe add reference */
	void AddRef()
	{
		Marked = false;
		asAtomicInc(RefCount);
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

		if (Context != nullptr)
		{
			Context->Release();
			Context = nullptr;
		}
	}
	/* For garbage collector to mark */
	void SetFlag()
	{
		Marked = true;
	}
	/* For garbage collector to check mark */
	bool GetFlag()
	{
		return Marked;
	}
	/* For garbage collector to check reference count */
	int GetRefCount()
	{
		return RefCount;
	}
	/*
		Thread safe store function, this is used as promise resolver function,
		will either only store the result or store result and execute callback
		that will resume suspended context and then release the promise (won't destroy)
	*/
	void Store(void* RefPointer, int RefTypeId)
	{
		Update.lock();
		PROMISE_ASSERT(Value.TypeId == asTYPEID_VOID, "promise should be settled only once");
		PROMISE_ASSERT(RefPointer != nullptr, "input pointer should not be null");
		PROMISE_ASSERT(Engine != nullptr, "promise is malformed (engine is null)");
		PROMISE_ASSERT(Context != nullptr, "promise is malformed (context is null)");

		if (Value.TypeId == asTYPEID_VOID)
		{
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

			if (Context->GetUserData(PROMISE_USERID) == (void*)this)
				Context->SetUserData(nullptr, PROMISE_USERID);

			bool WantsResume = (Context->GetState() != asEXECUTION_ACTIVE);
			Update.unlock();
			if (WantsResume)
				Executor()(this, Context);
			else
				Release();
		}
		else
		{
			asIScriptContext* ThisContext = asGetActiveContext();
			if (!ThisContext)
				ThisContext = Context;

			ThisContext->SetException("promise is already fulfilled");
			Update.unlock();
		}
	}
	/* Thread safe store function, a little easier for C++ usage */
	void Store(void* RefPointer, const char* TypeName)
	{
		PROMISE_ASSERT(Engine != nullptr, "promise is malformed (engine is null)");
		PROMISE_ASSERT(TypeName != nullptr, "typename should not be null");
		Store(RefPointer, Engine->GetTypeIdByDecl(TypeName));
	}
	/* Thread safe retrieve function, non-blocking try-retrieve future value */
	bool Retrieve(void* RefPointer, int RefTypeId)
	{
		PROMISE_ASSERT(Engine != nullptr, "promise is malformed (engine is null)");
		PROMISE_ASSERT(RefPointer != nullptr, "output pointer should not be null");
		if (Value.TypeId == asTYPEID_VOID)
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
		if (Value.TypeId == asTYPEID_VOID)
			return nullptr;

		if (Value.TypeId & asTYPEID_OBJHANDLE)
			return &Value.Object;
		else if (Value.TypeId & asTYPEID_MASK_OBJECT)
			return Value.Object;
		else if (Value.TypeId <= asTYPEID_DOUBLE || Value.TypeId & asTYPEID_MASK_SEQNBR)
			return &Value.Integer;

		return nullptr;
	}
	/* Can be used to check if promise is still pending */
	bool IsPending()
	{
		return Value.TypeId == asTYPEID_VOID;
	}
	/*
		This function should be called before retrieving the value
		from promise, it will either suspend current context and add
		reference to this promise if it is still pending or do nothing
		if promise was already settled
	*/
	BasicPromise* YieldIf()
	{
		std::unique_lock<std::mutex> Unique(Update);
		if (Value.TypeId == asTYPEID_VOID && Context != nullptr)
		{
			AddRef();
			Context->SetUserData(this, PROMISE_USERID);
			Executor()(this);
			Context->Suspend();
		}

		return this;
	}

private:
	/*
		Construct a promise, notify GC, set value to none,
		grab a reference to script context
	*/
	BasicPromise(asIScriptContext* NewContext) noexcept : Engine(nullptr), Context(NewContext), RefCount(1), Marked(false)
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
	}

public:
	/* BasicPromise creation function, for use within C++ */
	static BasicPromise* Create()
	{
		return new(asAllocMem(sizeof(BasicPromise))) BasicPromise(asGetActiveContext());
	}
	/* BasicPromise creation function, for use within AngelScript */
	static BasicPromise* CreateFactory(void* _Ref, int TypeId)
	{
		BasicPromise* Future = new(asAllocMem(sizeof(BasicPromise))) BasicPromise(asGetActiveContext());
		if (TypeId != asTYPEID_VOID)
			Future->Store(_Ref, TypeId);

		return Future;
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
		promise will be an object handle with GC behaviours, default
		constructed promise will be pending otherwise early settled
	*/
	static void Register(asIScriptEngine* Engine)
	{
		using Type = BasicPromise<Executor>;
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
			if (U != '\"' && U != '\'')
			{
				if (Size - Offset < MatchSize || memcmp(Code + Offset, Match, MatchSize) != 0)
				{
					++Offset;
					continue;
				}
			}
			else
			{
				++Offset;
				while (Offset < Size)
				{
					if (Code[Offset++] == U)
						break;
				}

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
				/*
					Naive basic_string assign, replaces
					part of input text with generated code
					like this: text <new-code> text
				*/
				size_t RightSize = Size - Offset;
				char* Right = (char*)asAllocMem(RightSize);
				memcpy(Right, Code + End, RightSize);

				size_t ExpressionSize = (End - Start);
				const char GeneratorText[] = "." PROMISE_YIELD "()." PROMISE_UNWRAP "()";
				size_t MiddleSize = ExpressionSize + sizeof(GeneratorText) - 1;
				char* Middle = (char*)asAllocMem(MiddleSize);
				memcpy(Middle, Code + Start, MiddleSize);
				memcpy(Middle + ExpressionSize, GeneratorText, sizeof(GeneratorText) - 1);

				size_t LeftSize = Offset;
				char* Left = (char*)asAllocMem(LeftSize + 1);
				memcpy(Left, Code, LeftSize);
				Left[LeftSize++] = '\0';

				char* Substring = (char*)asAllocMem(LeftSize + MiddleSize + RightSize);
				memcpy(Substring, Left, LeftSize);
				memcpy(Substring + LeftSize - 1, Middle, MiddleSize);
				memcpy(Substring + LeftSize + MiddleSize - 1, Right, RightSize);
				asFreeMem(Left);
				asFreeMem(Middle);
				asFreeMem(Right);
				asFreeMem(Code);

				Code = Substring;
				Size = strlen(Code);
				Offset += MiddleSize;
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
struct SequentialExecutor
{
	/* Called before suspend, this method will probably be optimized out */
	inline void operator()(BasicPromise<SequentialExecutor>* BasicPromise)
	{
	}
	/* Called after suspend, this method will probably be inlined anyways */
	inline void operator()(BasicPromise<SequentialExecutor>* BasicPromise, asIScriptContext* Context)
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
		BasicPromise->Release();
	}
};

using SeqPromise = BasicPromise<SequentialExecutor>;
#endif