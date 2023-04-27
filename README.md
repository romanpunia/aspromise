## About
A header-only implementation of promise concept for AngelScript library. Actual implementation is in __promise.hpp__ file.

Promise object it self is pretty lightweight, it follows guarantees provided by **\<any\>** class.
From design standpoint it provides pretty simple but effective API: **get/set/pending** functions.
Implementation works in a way that allows one to never block for waiting. This could be used for
effective task processing in concurrent environments. AngelScript implements coroutines concept which
is highly utilized by this promise interface.

AngelScript engine will work with class as with GC watched object handle. Thread safe promise settlement (resolve)
is guaranteed, thus this promise implementation avoids exceptions not for performance penalty but rather because
they are strings in AngelScript. This behaviour is controlled by user anyways and can be implemented fast.

Promise class is a template for a reason, it needs a specific functor struct that will be called before context suspend
and when resume is requested. This allows one to implement promise execution in any manner: using thread pool, conditional variables, single threaded sequence of execute calls and using other techniques that could be required by their specific environment. This also allows informative debugging with watchers.

Implementation does not have some features from other languages like JavaScript, for example **Promise.all**, these could be added through script file easily. Also promise does not contain **\<then\>** function that is used pretty often in JavaScript. That is because unlike JavaScript in AngelScript every context of execution is it self a coroutine so that is considered bloat by my self to add chaining.

## Core built-in dependencies
* [AngelScript](https://sourceforge.net/projects/angelscript/)

## Building
CMake is build-system for this project, use CMake generate feature, no additional setup is required.

## License
Project is licensed under the MIT license. Free for any type of use.