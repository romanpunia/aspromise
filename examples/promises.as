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
    print_set_timeout(500);
    co_await set_timeout_native_promise(500); // Promise is created and resolved in C++, awaited in AS

    print_set_timeout(100);
    auto indirect_timer = set_timeout(100); // Promise is created, resolved and awaited in AS
    indirect_timer.when(function(Value)
    {
        print_resolve_timeout_async(Value);
    });

    co_await indirect_timer; // Can be both awaited and callback fired at the same time

    print_set_timeout(1000);
    co_await set_timeout(1000);

    print_set_timeout(500);
    co_await set_timeout(500);

    print_set_timeout(1350);
    await_promise_blocking(set_timeout(1350)); // await promise within C++ (blocking)
    
    print_set_timeout(1050);
    await_promise_non_blocking(set_timeout(1050)); // await promise within C++ (non-blocking)

    print_set_timeout(1000);
    uint32 switches = co_await set_timeout(1000); // co_await returns stored value by the way
    
    auto end = get_milliseconds();
    print_and_wait_for_input(end - start, switches);
}