#include <Jade/Log.hpp>

#include "internal/Events/EventDispatcher.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class EventLogCapture final {
public:
  explicit EventLogCapture(Jade::Log::Level level = Jade::Log::Level::Debug)
  {
    Jade::Log::Config config;
    config.minimumLevel = level;
    config.console = false;
    config.sink = [this](const auto& record) {
      if (record.category != "Events") return;
      records.push_back(record);
      if (onRecord) onRecord(record);
    };
    Jade::Log::Configure(std::move(config));
  }
  ~EventLogCapture() { Jade::Log::Configure(); }

  std::vector<Jade::Log::Record> records;
  std::function<void(const Jade::Log::Record&)> onRecord;
};

} // namespace

TEST(EventLogging, DebugRecordsBindingsEdgesAndConsumption)
{
  EventLogCapture capture;
  Jade::Detail::EventDispatcher events;
  int firstCalls = 0, lastCalls = 0;
  const auto chord = Jade::KeyPressed<"C-S-w">::chord;
  events.Connect(Jade::KeyAction::Pressed, chord, [&](const auto&) {
    ++firstCalls;
    return Jade::EventFlow::Continue;
  });
  events.Connect(Jade::KeyAction::Pressed, chord,
    [](const auto&) { return Jade::EventFlow::Stop; });
  events.Connect(Jade::KeyAction::Pressed, chord, [&](const auto&) {
    ++lastCalls;
    return Jade::EventFlow::Continue;
  });
  events.Connect(Jade::MouseAction::Released, Jade::MouseButton::Button8,
    [](const auto&) { return Jade::EventFlow::Continue; });
  events.Connect(Jade::KeyAction::Released, Jade::KeyReleased<"F12">::chord,
    [](const auto&) { return Jade::EventFlow::Continue; });
  ASSERT_EQ(capture.records.size(), 5u);
  EXPECT_EQ(capture.records[0].message, "Subscribed: id=1 binding=KeyPressed<C-S-W>");
  EXPECT_EQ(capture.records[3].message, "Subscribed: id=4 binding=MouseReleased<Button8>");
  EXPECT_EQ(capture.records[4].message, "Subscribed: id=5 binding=KeyReleased<F12>");
  capture.records.clear();

  EXPECT_EQ(events.Dispatch(Jade::KeyAction::Pressed, {chord.key, chord.modifiers}), Jade::EventFlow::Stop);
  EXPECT_EQ(events.Dispatch(Jade::KeyAction::Pressed, {chord.key, Jade::Modifiers::None}), Jade::EventFlow::Continue);
  EXPECT_EQ(events.Dispatch(Jade::MouseAction::Released,
    {Jade::MouseButton::Button8, Jade::Modifiers::Alt}), Jade::EventFlow::Continue);
  EXPECT_EQ(events.Dispatch(Jade::KeyAction::Released, {Jade::Key::F12}), Jade::EventFlow::Continue);
  ASSERT_EQ(capture.records.size(), 4u);
  EXPECT_EQ(capture.records[0].message, "KeyPressed: binding=C-S-W handlers=2 consumed_by=2");
  EXPECT_EQ(capture.records[1].message, "KeyPressed: binding=W handlers=0 consumed_by=0");
  EXPECT_EQ(capture.records[2].message, "MouseReleased: button=Button8 handlers=1 consumed_by=0");
  EXPECT_EQ(capture.records[3].message, "KeyReleased: binding=F12 handlers=1 consumed_by=0");
  for (const auto& record : capture.records) EXPECT_EQ(record.level, Jade::Log::Level::Debug);
  EXPECT_EQ(firstCalls, 1);
  EXPECT_EQ(lastCalls, 0);
}

TEST(EventLogging, HighFrequencyEventsRequireTrace)
{
  EventLogCapture capture;
  Jade::Detail::EventDispatcher events;
  events.Connect(Jade::KeyAction::Held, Jade::KeyHeld<"w">::chord,
    [](const auto&) { return Jade::EventFlow::Stop; });
  events.Connect(Jade::MouseAction::Held, Jade::MouseButton::Left,
    [](const auto&) { return Jade::EventFlow::Continue; });
  events.Connect([](const Jade::MouseMoved&) { return Jade::EventFlow::Continue; });
  events.Connect([](const Jade::MouseScrolled&) { return Jade::EventFlow::Continue; });
  events.Connect([](const Jade::FrameUpdate&) { return Jade::EventFlow::Continue; });
  capture.records.clear();
  const auto dispatch = [&] {
    events.Dispatch(Jade::KeyAction::Held, {Jade::Key::W, Jade::Modifiers::None, 0.04f});
    events.Dispatch(Jade::MouseAction::Held, {Jade::MouseButton::Left, Jade::Modifiers::None, 0.04f});
    events.Dispatch(Jade::MouseMoved{100, 200, 2, -3});
    events.Dispatch(Jade::MouseScrolled{0.25, -0.5});
    events.Dispatch(Jade::FrameUpdate{0.04f, 0.5, 12});
  };
  dispatch();
  EXPECT_TRUE(capture.records.empty());
  Jade::Log::SetLevel(Jade::Log::Level::Trace);
  dispatch();
  ASSERT_EQ(capture.records.size(), 5u);
  EXPECT_EQ(capture.records[0].message, "KeyHeld: binding=W dt=0.0400s handlers=1 consumed_by=1");
  EXPECT_EQ(capture.records[1].message, "MouseHeld: button=Left dt=0.0400s handlers=1 consumed_by=0");
  EXPECT_EQ(capture.records[2].message, "MouseMoved: x=100.00 y=200.00 dx=2.00 dy=-3.00 handlers=1 consumed_by=0");
  EXPECT_EQ(capture.records[3].message, "MouseScrolled: dx=0.250 dy=-0.500 handlers=1 consumed_by=0");
  EXPECT_EQ(capture.records[4].message, "FrameUpdate: frame=12 dt=0.0400s handlers=1 consumed_by=0");
  for (const auto& record : capture.records) EXPECT_EQ(record.level, Jade::Log::Level::Trace);
  capture.records.clear();
  Jade::Log::SetLevel(Jade::Log::Level::Info);
  dispatch();
  events.Dispatch(Jade::KeyAction::Pressed, {Jade::Key::W});
  EXPECT_TRUE(capture.records.empty());
}

TEST(EventLogging, FailuresIdentifyTheSubscriptionAndPreserveExceptions)
{
  EventLogCapture capture(Jade::Log::Level::Info);
  Jade::Detail::EventDispatcher events;
  const auto standard = events.Connect([](const Jade::FrameUpdate&) -> Jade::EventFlow {
    throw std::runtime_error("callback failed");
  });
  EXPECT_THROW(events.Dispatch(Jade::FrameUpdate{}), std::runtime_error);
  ASSERT_EQ(capture.records.size(), 1u);
  EXPECT_EQ(capture.records.back().level, Jade::Log::Level::Error);
  EXPECT_EQ(capture.records.back().message,
    "Handler failed: event=FrameUpdate subscription=1 binding=FrameUpdate reason=callback failed");
  *standard = false;
  const auto other = events.Connect([](const Jade::FrameUpdate&) -> Jade::EventFlow { throw 42; });
  EXPECT_THROW(events.Dispatch(Jade::FrameUpdate{}), int);
  ASSERT_EQ(capture.records.size(), 2u);
  EXPECT_EQ(capture.records.back().message,
    "Handler failed: event=FrameUpdate subscription=2 binding=FrameUpdate reason=non-standard exception");
  capture.records.clear();
  Jade::Log::SetLevel(Jade::Log::Level::Off);
  EXPECT_THROW(events.Dispatch(Jade::FrameUpdate{}), int);
  EXPECT_TRUE(capture.records.empty());
  *other = false;
  EXPECT_EQ(events.Dispatch(Jade::FrameUpdate{}), Jade::EventFlow::Continue);
}

TEST(EventLogging, CancellationAndRegistrationKeepCountsAndSnapshotsAccurate)
{
  EventLogCapture capture(Jade::Log::Level::Trace);
  Jade::Detail::EventDispatcher events;
  std::shared_ptr<bool> second;
  int lateCalls = 0;
  const auto first = events.Connect([&](const Jade::FrameUpdate&) {
    *second = false;
    events.Connect([&](const Jade::FrameUpdate&) { ++lateCalls; return Jade::EventFlow::Continue; });
    return Jade::EventFlow::Continue;
  });
  second = events.Connect([](const Jade::FrameUpdate&) { return Jade::EventFlow::Continue; });
  capture.records.clear();
  events.Dispatch(Jade::FrameUpdate{});
  ASSERT_EQ(capture.records.size(), 3u);
  EXPECT_EQ(capture.records[0].message, "Disconnected: id=2 binding=FrameUpdate");
  EXPECT_EQ(capture.records[1].message, "Subscribed: id=3 binding=FrameUpdate");
  EXPECT_TRUE(capture.records[2].message.ends_with("handlers=1 consumed_by=0"));
  EXPECT_EQ(lateCalls, 0);
  *first = false;
  capture.records.clear();
  events.Dispatch(Jade::FrameUpdate{});
  ASSERT_EQ(capture.records.size(), 2u);
  EXPECT_EQ(capture.records[0].message, "Disconnected: id=1 binding=FrameUpdate");
  EXPECT_TRUE(capture.records[1].message.ends_with("handlers=1 consumed_by=0"));
  EXPECT_EQ(lateCalls, 1);
  capture.records.clear();
  events.Dispatch(Jade::FrameUpdate{});
  ASSERT_EQ(capture.records.size(), 1u);
  EXPECT_EQ(lateCalls, 2);
}

TEST(EventLogging, LoggingSinkCanRegisterDuringConnectionAndPruning)
{
  EventLogCapture capture;
  Jade::Detail::EventDispatcher events;
  std::vector<int> calls;
  std::shared_ptr<bool> second;
  capture.onRecord = [&](const auto& record) {
    if (record.message == "Subscribed: id=1 binding=FrameUpdate") {
      second = events.Connect([&](const Jade::FrameUpdate&) {
        calls.push_back(2); return Jade::EventFlow::Continue;
      });
    } else if (record.message == "Disconnected: id=1 binding=FrameUpdate") {
      events.Connect([&](const Jade::FrameUpdate&) {
        calls.push_back(3); return Jade::EventFlow::Continue;
      });
    }
  };
  const auto first = events.Connect([&](const Jade::FrameUpdate&) {
    calls.push_back(1); return Jade::EventFlow::Continue;
  });
  events.Dispatch(Jade::FrameUpdate{});
  EXPECT_EQ(calls, (std::vector<int>{1, 2}));
  *first = false;
  *second = false;
  events.Dispatch(Jade::FrameUpdate{});
  EXPECT_EQ(calls, (std::vector<int>{1, 2, 3}));
  ASSERT_EQ(capture.records.size(), 3u);
  EXPECT_EQ(capture.records[1].message, "Disconnected: id=1 binding=FrameUpdate");
  EXPECT_EQ(capture.records[2].message, "Disconnected: id=2 binding=FrameUpdate");
}
