uint32 timers_processed = 0;

class timeout_event
{
    promise<int>@ data = promise<int>();

    void settle()
    {
        print_resolve_timeout();
        data.wrap(++timers_processed);
    }
}

promise<int>@ set_timeout(uint64 timeout_ms)
{
    timeout_event@ event = timeout_event();
    set_timeout_native(timeout_ms, timer_callback(event.settle));
    return event.data;
}

void main()
{
    auto start = get_milliseconds();
    print_set_timeout(1000);
    co_await set_timeout(1000);

    print_set_timeout(500);
    co_await set_timeout(500);

    print_set_timeout(1000);
    uint32 switches = co_await set_timeout(1000);

    auto end = get_milliseconds();
    print_and_wait_for_input(end - start, switches);
}