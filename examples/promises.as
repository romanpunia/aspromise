uint32 timers_processed = 1;

class timeout_event
{
    promise<int32>@ data = promise<int32>();

    void settle()
    {
        print_resolve_timeout();
        data.wrap(++timers_processed);
    }
}

promise<int32>@ set_timeout(uint64 timeout_ms)
{
    timeout_event@ event = timeout_event();
    set_timeout_native(timeout_ms, timer_callback(event.settle));
    return event.data;
}

void main()
{
    auto start = get_milliseconds();
    co_await set_timeout_native_promise(500); // Promise is created and resolved in C++, awaited in AS

    auto indirect_timer = set_timeout(100); // Promise is created, resolved and awaited in AS
    indirect_timer.when(function(value)
    {
        print_resolve_timeout_async(value);
    });

    /* Can be both awaited and callback fired at the same time */
    co_await indirect_timer;
    co_await set_timeout(1000);
    co_await set_timeout(500);

    auto blocking_timer = set_timeout(1350);
    try 
    {
        await_promise_blocking(@blocking_timer); // await promise within C++ (blocking)
    }
    catch
    {
        // if we use Node.js like event loop then this type of awaiting is disallowed
        await_promise_non_blocking(@blocking_timer);
    }
    
    await_promise_non_blocking(set_timeout(1050)); // await promise within C++ (non-blocking)
    uint32 switches = co_await set_timeout(1000); // co_await returns stored value by the way
    
    auto end = get_milliseconds();
    print_and_wait_for_input(end - start, switches);
}