#pragma once
// The event bridge's view of the shared queue and dispatch code (shared/dispatch).
#include <dispatch/DispatchBacklog.hpp>
#include <dispatch/DispatchBudget.hpp>
#include <dispatch/QueueBuffers.hpp>
#include <dispatch/QueueCapacity.hpp>
#include <dispatch/QueueDispatchSchedule.hpp>

namespace UE4SSLuaEventBridge {
using UE4SSXB::ConsumerBacklogCount;
using UE4SSXB::default_queue_checks_per_second;
using UE4SSXB::dispatch_budgeted;
using UE4SSXB::DispatchBacklog;
using UE4SSXB::DispatchBudget;
using UE4SSXB::drain_queue;
using UE4SSXB::parse_queue_check_rate;
using UE4SSXB::QueueCapacity;
using UE4SSXB::QueueDispatchSchedule;
using UE4SSXB::QueueReadiness;
using UE4SSXB::retain_empty_buffer;
}
