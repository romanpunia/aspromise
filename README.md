## About
A header-only implementation of promise concept for AngelScript library. Actual implementation is in __promise.hpp__ file, see example usage (examples directory): is in __promises.cpp__ and __promises.as__

## Usage
Drag and drop __promise.hpp__ somewhere into your project.

## Example usage with AngelScript
Promise creation
```as
    promise<int>@ result = promise<int>();
    // Use promise_v for promise<void>
```

Promise settlement
```cpp
    result.wrap(10);
```

Promise awaiting using coroutines
```cpp
    int number_await = co_await result;
    int number_unwrap = result.yield().unwrap();
```

Promise awaiting using callbacks
```js
    result.when(function(number) { });
```

## Example usage with C++
Promise creation
```cpp
    AsBasicPromise<Executor>* Result = AsBasicPromise<Executor>::Create();
    /*
        Built-in implementations:
            AsDirectPromise = thread that resolves the promise continues script execution,
            AsReactivePromise = thread that resolves notifies the initiator
    */
```

Promise settlement
```cpp
    int32_t Number = 10;
    Result->Store(&Number, asTYPEID_INT32);
```

Promise awaiting using wait
```cpp
    int32_t Number;
    Result->WaitIf()->Retrieve(&Number, asTYPEID_INT32);
```

Promise awaiting using callbacks
```cpp
    Result->When([](AsBasicPromise<Executor>* Result)
    {
        int32_t Number;
        Result->Retrieve(&Number, asTYPEID_INT32);
    });
```

## Details
Promise object it self is measurably lightweight, it follows guarantees provided by **\<any\>** class.
From design standpoint it provides pretty simple but effective API: **get/set/pending** functions.
Implementation works in a way that allows one to never block for waiting. This could be used for
effective task processing in concurrent environments. AngelScript implements coroutines concept which
is highly utilized by this promise interface.

AngelScript engine will work with class as with GC watched object handle. Thread safe promise settlement (resolve)
is guaranteed, this promise implementation avoids exceptions not because of performance penalty but rather because
they are strings in AngelScript. This behaviour is controlled by user anyways and can be implemented fast.

Promise class is a template for a reason, it needs a specific functor struct that will be called before context suspend
and when context resume is requested. This allows one to implement promise execution in any manner: using thread pool, conditional variables, single threaded sequence of execute calls and using other techniques that could be required by their specific environment. This also allows informative debugging with watchers.

Implementation does not have some features from other languages like JavaScript, for example **Promise.all**, these could be added through script file. Also promise does not contain **\<then\>** function that is used pretty often in JavaScript. That is because unlike JavaScript in AngelScript every context of execution is it self a coroutine so that is considered bloat by my self to add chaining.

Promise execution is conditional meaning early settled promises will never suspend context which improves performance and reduces latency. Also promise implementation uses AngelScript's memory functions to ensure support for memory pools and other optimizations.

This implementation supports important feature in my opinion: __co_await__ keyword brought directly from C++20, it works just like __await__ keyword in JavaScript but anywhere. This feature is not (yet?) AngelScript compiler supported so it requires an extra step over source code of script before sending it to compiler. See following usage examples:
```cpp
    promise<...>@ future = ...;
    co_await future; // future could be a function call
    co_await  (    future    );
    co_await (future);
    co_await co_await future; // nested await
    co_await future[0].run_job(); // if future is an array
    auto@ response = (co_await future) + "output"; // if future has plus op
    if ((co_await (future)).is_succcess);
    while ((co_await future).is_pending);

    co_await(future) // Not supported, space is required
```

And final feature is naming customization, modifying preprocessor definitions in __promise.hpp__ you could achieve desired naming conventions. By default C style is used (snake-case). 

## How it executes
This example has two implementations for promise resolution (controlled by __ExecutionPolicy__ global variable in **examples/promises.cpp**):
* Settlement thread executes next:
    1. Thread A has started the execution
    2. Promise await is called
    3. Thread A sleeps or does something unrelated
    4. Thread B settles the promise
    5. Thread B continues the execution
* Node.js event loop executes next:
    1. Thread A has started the execution
    2. Promise await is called
    3. Thread A sleeps or does something unrelated
    4. Thread B settles the promise
    5. Thread B pushes a callback into a callback queue
    5. Thread A awakes or reaches it's event loop
    6. Thread A pops latest callback from a callback queue
    7. Thread A continues the execution

## Building
CMake is build-system for this project, use CMake generate feature, no additional setup is required.

## License
Project is licensed under the MIT license. Free for any type of use.