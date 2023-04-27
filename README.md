## About
A header-only implementation of promise concept for AngelScript library. Actual implementation is in __promise.hpp__ file, example usage is in __app.cpp__ and __promises.as__ (bin directory)

Promise object it self is pretty lightweight, it follows guarantees provided by **\<any\>** class.
From design standpoint it provides pretty simple but effective API: **get/set/pending** functions.
Implementation works in a way that allows one to never block for waiting. This could be used for
effective task processing in concurrent environments. AngelScript implements coroutines concept which
is highly utilized by this promise interface.

AngelScript engine will work with class as with GC watched object handle. Thread safe promise settlement (resolve)
is guaranteed, this promise implementation avoids exceptions not because of performance penalty but rather because
they are strings in AngelScript. This behaviour is controlled by user anyways and can be implemented fast.

Promise class is a template for a reason, it needs a specific functor struct that will be called before context suspend
and when context resume is requested. This allows one to implement promise execution in any manner: using thread pool, conditional variables, single threaded sequence of execute calls and using other techniques that could be required by their specific environment. This also allows informative debugging with watchers.

Implementation does not have some features from other languages like JavaScript, for example **Promise.all**, these could be added through script file easily. Also promise does not contain **\<then\>** function that is used pretty often in JavaScript. That is because unlike JavaScript in AngelScript every context of execution is it self a coroutine so that is considered bloat by my self to add chaining.

Promise execution is conditional meaning early settled promises will never suspend context which improves performance and reduces latency. Also promise implementation uses AngelScript's memory functions to ensure support for memory pools and other optimizations.

This implementation supports important feature in my opinion: __co_await__ keyword brought directly from C++20, it works just like __await__ keyword in JavaScript but anywhere. This feature is not (yet?) AngelScript compiler supported so it requires an extra step over source code of script before sending it to compiler. See following usage examples:
```cpp
    promise<http_response@>@ future = ...;
    co_await future; // future could be a function call
    co_await (future);
    auto@ response = (co_await future) + "output"; // if http_response has plus op
    if ((co_await (future)).status == 200);
    while ((co_await future).is_socket_reading);

    co_await(future) // Not supported, space is required
```

And final feature is naming customization, modifying preprocessor definitions in __promise.hpp__ you could achieve desired naming conventions. By default C style is used (snake-case). 

In this example promise execution chain is multithreaded. __Execute()__ is called in thread A, then __co_await__ is called, thread A is now out of execution, some random thread B settles the promise and then thread B continues execution of script context.

## Core built-in dependencies
* [AngelScript](https://sourceforge.net/projects/angelscript/)

## Building
CMake is build-system for this project, use CMake generate feature, no additional setup is required.

## License
Project is licensed under the MIT license. Free for any type of use.