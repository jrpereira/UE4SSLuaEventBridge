#include <UE4SSLuaEventBridge/QueueBuffers.hpp>
#include <UE4SSLuaEventBridge/QueueDispatchSchedule.hpp>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using namespace UE4SSLuaEventBridge;
struct Event { int sequence; Clock::time_point enqueued; };
double us(Clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }
double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>(std::ceil(fraction * values.size())) - 1];
}
double sum(const std::vector<double>& values)
{
    double result = 0;
    for (double value : values) result += value;
    return result;
}
void checked(lua_State* lua, int status)
{
    if (status != LUA_OK) {
        const char* message = lua_tostring(lua, -1);
        throw std::runtime_error(message ? message : "Lua error");
    }
}
int positive(const char* value, int maximum)
{
    const std::string text(value);
    size_t end{};
    int result = std::stoi(text, &end);
    if (end != text.size() || result < 1 || result > maximum)
        throw std::runtime_error("Invalid positive integer argument");
    return result;
}
void call(lua_State* lua, int dispatcher, int sequence, double& callbackUs, double& dispatchUs)
{
    const auto start = Clock::now();
    lua_rawgeti(lua, LUA_REGISTRYINDEX, dispatcher);
    lua_pushinteger(lua, 1); // token
    lua_pushinteger(lua, 1); // subscription
    lua_pushliteral(lua, "/Benchmark/SyntheticAction");
    lua_pushliteral(lua, "Triggered");
    lua_pushnumber(lua, 0.1);
    lua_pushnumber(lua, 0.1);
    lua_pushnumber(lua, 1.0);
    lua_pushnumber(lua, 0.0);
    lua_pushnumber(lua, 0.0);
    lua_pushinteger(lua, 0);
    lua_pushinteger(lua, sequence);
    const auto beforeCall = Clock::now();
    const int status = lua_pcall(lua, 11, 0, 0);
    const auto end = Clock::now();
    checked(lua, status);
    callbackUs = us(end - beforeCall);
    dispatchUs = us(end - start);
}
int main(int argc, char** argv)
{
    try {
        if (argc != 6) throw std::runtime_error(
            "Usage: BridgeFlowBenchmark seconds_per_stage repeats passes_per_second callback.lua output.csv (run from repo root)");
        const int seconds = positive(argv[1], 300);
        const int repeats = positive(argv[2], 100);
        const int passes = positive(argv[3], 1000);
        std::ofstream out(argv[5]);
        if (!out) throw std::runtime_error("Cannot open output CSV");
        out << "repeat,target_eps,events,actual_eps,passes_per_second,callback_total_ms,callback_ms_per_second,callback_mean_us,callback_p95_us,callback_p99_us,callback_max_us,dispatch_mean_us,batch_p95_us,batch_max_us,queue_p95_ms,queue_max_ms,max_batch,elapsed_seconds\n";
        out << std::fixed << std::setprecision(6);
        for (int repeat = 1; repeat <= repeats; ++repeat) {
            for (int rate = 10; rate <= 100; rate += 10) {
                std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
                auto* lua = state.get();
                if (!lua) throw std::runtime_error("Cannot create Lua state");
                luaL_openlibs(lua);
                checked(lua, luaL_dostring(lua,
                    "__UE4SSLuaEventBridge_SessionId=1; "
                    "function UE4SSLuaEventBridge_BindAction(...) return 1 end"));
                checked(lua, luaL_loadfile(lua, "mod/lua/bridge_api.lua"));
                checked(lua, lua_pcall(lua, 0, 1, 0));
                const int dispatcher = luaL_ref(lua, LUA_REGISTRYINDEX);
                checked(lua, luaL_loadfile(lua, argv[4]));
                checked(lua, lua_pcall(lua, 0, 1, 0));
                if (!lua_isfunction(lua, -1)) throw std::runtime_error("Callback file must return a function");
                lua_setglobal(lua, "benchmark_callback");
                checked(lua, luaL_dostring(lua,
                    "assert(UE4SSLuaEventBridge.BindAction(1, '/Benchmark/SyntheticAction', "
                    "'Triggered', benchmark_callback))"));
                // Warm dispatcher and callback; negative sequences precede measured events.
                for (int i = -1000; i < 0; ++i) {
                    double a{}, b{};
                    call(lua, dispatcher, i, a, b);
                }
                std::mutex mutex;
                QueueReadiness ready;
                std::vector<Event> queue, spare;
                std::vector<double> callback, dispatch, batchTimes, waits;
                const int expected = rate * seconds;
                for (auto* samples : {&callback, &dispatch, &batchTimes, &waits})
                    samples->reserve(static_cast<size_t>(expected));
                std::atomic_bool cancel{false};
                const auto start = Clock::now() + 20ms;
                int maxBatch = 0;
                QueueDispatchSchedule schedule(static_cast<uint32_t>(passes));
                std::jthread producer([&] {
                    for (int i = 0; i < expected && !cancel.load(); ++i) {
                        std::this_thread::sleep_until(start + std::chrono::nanoseconds(
                            static_cast<long long>(i) * 1000000000LL / rate));
                        std::scoped_lock lock(mutex);
                        queue.push_back({i + 1, Clock::now()});
                        ready.publish();
                    }
                });
                try {
                    while (static_cast<int>(callback.size()) < expected) {
                        const auto now = Clock::now();
                        if (now > start + std::chrono::seconds(seconds + 30))
                            throw std::runtime_error("Benchmark timed out draining events");
                        if (now >= start && schedule.due(now) && ready.pending()) {
                            const auto batchStart = Clock::now();
                            std::vector<Event> batch;
                            {
                                std::scoped_lock lock(mutex);
                                batch = drain_queue(queue, spare);
                                ready.drained();
                            }
                            maxBatch = std::max(maxBatch, static_cast<int>(batch.size()));
                            for (const auto& event : batch) {
                                if (event.sequence != static_cast<int>(callback.size()) + 1)
                                    throw std::runtime_error("Lost or reordered event");
                                waits.push_back(us(Clock::now() - event.enqueued));
                                double a{}, b{};
                                call(lua, dispatcher, event.sequence, a, b);
                                callback.push_back(a);
                                dispatch.push_back(b);
                            }
                            batch.clear();
                            {
                                std::scoped_lock lock(mutex);
                                retain_empty_buffer(spare, batch);
                            }
                            batchTimes.push_back(us(Clock::now() - batchStart));
                        }
                        std::this_thread::sleep_for(5ms); // approximate UE4SS host cadence
                    }
                } catch (...) { cancel.store(true); throw; }
                producer.join();
                const double elapsed = std::max(static_cast<double>(seconds),
                    std::chrono::duration<double>(Clock::now() - start).count());
                const double totalMs = sum(callback) / 1000.0;
                out << repeat << ',' << rate << ',' << callback.size() << ','
                    << expected / elapsed << ',' << passes << ',' << totalMs << ','
                    << totalMs / elapsed << ',' << sum(callback) / callback.size() << ','
                    << percentile(callback, .95) << ',' << percentile(callback, .99) << ','
                    << percentile(callback, 1) << ',' << sum(dispatch) / dispatch.size() << ','
                    << percentile(batchTimes, .95) << ',' << percentile(batchTimes, 1) << ','
                    << percentile(waits, .95) / 1000 << ',' << percentile(waits, 1) / 1000 << ','
                    << maxBatch << ',' << elapsed << '\n';
                out.flush();
                std::cout << "Repeat " << repeat << ", " << rate << " events/s: "
                    << totalMs / elapsed << " ms/s in Lua, p95 "
                    << percentile(callback, .95) << " us; " << callback.size()
                    << '/' << expected << " delivered" << std::endl;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}